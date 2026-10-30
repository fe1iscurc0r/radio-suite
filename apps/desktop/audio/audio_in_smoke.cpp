// SPDX-License-Identifier: AGPL-3.0-or-later
// audio_in_smoke — 麦克风采集冒烟：先丢 ~1s 预热，再测 2s 稳态的帧节奏与样本预算。
// 首帧延迟（设备启动开销）单独报告，不计入稳态统计——WASAPI 共享模式首帧常在
// 0.4~0.5s 才到，若把它算进去会把预算百分比压到 ~57%，看起来像丢样。
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <thread>

#include "audio/audio_in.h"

using radio::audio::AudioIn;
using Clock = std::chrono::steady_clock;

namespace {
constexpr double kWarmupSecs = 1.0;
constexpr double kMeasureSecs = 2.0;
constexpr double kFpsMin = 40.0;   // 50 ± 20%，留给调度抖动
constexpr double kFpsMax = 60.0;
constexpr double kBudgetMin = 88.0;  // 稳态样本预算下限（%）
}  // namespace

int main() {
  AudioIn in;
  const auto devices = in.deviceNames();
  const auto ids = in.inputDeviceIds();
  if (devices.empty() || ids.empty()) {
    std::printf("FAIL: no input device\n");
    return 1;
  }
  std::printf("input devices: %zu, using [0] %s (id=%u)\n", devices.size(),
              devices[0].c_str(), ids[0]);

  const auto t0 = Clock::now();
  std::atomic<double> first_cb{-1.0};
  std::atomic<double> peak{0.0};
  std::atomic<int> warm_frames{0};
  std::atomic<int> frames{0};
  std::atomic<long long> samples{0};
  std::atomic<double> win_first{-1.0};
  std::atomic<double> win_last{-1.0};

  std::string err;
  const bool ok = in.start(
      ids[0],
      [&](std::span<const int16_t> frame) {
        const double t = std::chrono::duration<double>(Clock::now() - t0).count();
        double first = first_cb.load(std::memory_order_relaxed);
        if (first < 0.0) {
          first = t;
          first_cb.store(first, std::memory_order_relaxed);
        }
        double p = peak.load(std::memory_order_relaxed);
        for (int16_t v : frame) p = std::max(p, std::abs(double(v)));
        peak.store(p, std::memory_order_relaxed);
        if (t < first + kWarmupSecs) {
          warm_frames.fetch_add(1, std::memory_order_relaxed);
          return;
        }
        if (win_first.load(std::memory_order_relaxed) < 0.0)
          win_first.store(t, std::memory_order_relaxed);
        win_last.store(t, std::memory_order_relaxed);
        frames.fetch_add(1, std::memory_order_relaxed);
        samples.fetch_add(static_cast<long long>(frame.size()),
                          std::memory_order_relaxed);
      },
      &err);
  if (!ok) {
    std::printf("FAIL: start failed: %s\n", err.c_str());
    return 1;
  }

  // 窗口以首帧为原点：先等首帧（设备启动可能 0.5s），再等预热 + 稳态 + 余量
  const double deadline = 3.0;
  while (first_cb.load() < 0.0 &&
         std::chrono::duration<double>(Clock::now() - t0).count() < deadline)
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  if (first_cb.load() >= 0.0) {
    const auto target = t0 + std::chrono::duration_cast<Clock::duration>(
                                 std::chrono::duration<double>(
                                     first_cb.load() + kWarmupSecs + kMeasureSecs +
                                     0.2));
    std::this_thread::sleep_until(target);
  }
  in.stop();

  const double latency = first_cb.load();
  if (latency < 0.0) {
    std::printf("FAIL: no capture callback within %.1fs\n",
                kWarmupSecs + kMeasureSecs + 0.3);
    return 1;
  }
  const int n_frames = frames.load();
  const long long n_samples = samples.load();
  const double span = win_last.load() - win_first.load();
  std::printf("first callback after %.3fs (device startup latency)\n", latency);
  std::printf("warm-up %.1fs: %d frames discarded\n", kWarmupSecs,
              warm_frames.load());
  if (n_frames < 2 || span <= 0.0) {
    std::printf("FAIL: steady window has %d frames (span %.3fs)\n", n_frames,
                span);
    return 1;
  }

  // 帧间隔个数 = 帧数 - 1，对应时间跨度 span；稳态应为 50 帧/秒（含首端点）
  const double fps = (n_frames - 1) / span;
  const double expect_frames = span * (radio::audio::kTxCaptureRate /
                                       double(radio::audio::kTxFrameSamples)) + 1.0;
  const double expect_samples = expect_frames * radio::audio::kTxFrameSamples;
  const double budget = 100.0 * n_frames / expect_frames;
  std::printf("steady window %.2fs: %d frames -> %.1f fps (expect ~50), "
              "peak=%d, overflows=%zu\n",
              span, n_frames, fps, int(peak.load()), in.overflows());
  std::printf("sample budget: %lld / %.0f (%.1f%%)\n", n_samples,
              expect_samples, budget);

  if (fps < kFpsMin || fps > kFpsMax) {
    std::printf("FAIL: frame rate out of range [%.0f, %.0f]\n", kFpsMin,
                kFpsMax);
    return 1;
  }
  if (budget < kBudgetMin) {
    std::printf("FAIL: sample budget below %.0f%%\n", kBudgetMin);
    return 1;
  }
  std::printf("OK: steady-state pacing and sample budget within tolerance "
              "(level silence-check skipped)\n");
  return 0;
}
