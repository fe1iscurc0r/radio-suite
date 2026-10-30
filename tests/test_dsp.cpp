// SPDX-License-Identifier: AGPL-3.0-or-later
// DSP 测试：FFT 精度 / 瀑布处理 / 合成信号（TEST_CASE 名保持 ASCII，理由见 test_civ.cpp 头注）
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

#include "app/smeter_scale.h"
#include "dsp/fft.h"
#include "dsp/synth.h"
#include "dsp/waterfall_frame.h"

using namespace radio::dsp;
using Catch::Approx;

TEST_CASE("dsp: single tone peaks at expected bin", "[dsp]") {
  constexpr size_t N = 2048;
  constexpr double Fs = 48'000.0;
  SpectrumAnalyzer an(N, Fs);
  SignalSynth synth(N, Fs);
  synth.setTones({{7'500.0, 0.5f}});
  synth.setNoiseFloor(0.0f);  // 无噪，便于定位

  std::vector<float> buf(N);
  synth.fill(buf.data());
  an.process(buf.data(), N);

  const auto& db = an.powerDb();
  REQUIRE(db.size() == N / 2 + 1);
  const size_t peak = static_cast<size_t>(
      std::max_element(db.begin(), db.end()) - db.begin());
  const double expect = 7'500.0 / an.binHz();
  CHECK(static_cast<double>(peak) == Approx(expect).margin(2.0));
}

TEST_CASE("dsp: bin-centered tone has exact peak and low far leakage", "[dsp]") {
  constexpr size_t N = 256;
  constexpr double Fs = 1'000.0;
  SpectrumAnalyzer an(N, Fs);
  constexpr size_t kBin = 32;
  std::vector<float> tone(N);
  // 相位取 0、对准 bin 32：x[n] = cos(2π·32·n/N)
  for (size_t n = 0; n < N; ++n)
    tone[n] = static_cast<float>(std::cos(2.0 * 3.14159265358979323846 *
                                          static_cast<double>(kBin * n) / N));

  an.process(tone.data(), N);
  const auto& db = an.powerDb();

  const size_t peak = static_cast<size_t>(
      std::max_element(db.begin(), db.end()) - db.begin());
  CHECK(peak == kBin);

  float away = -300.0f;
  for (size_t k = 1; k + 1 < db.size(); ++k) {
    if (k + 2 < kBin || k > kBin + 2) away = std::max(away, db[k]);
  }
  CHECK(db[kBin] - away > 30.0f);  // Hann 窗远端泄漏抑制
}

TEST_CASE("dsp: waterfall EMA / peak-hold / row bytes", "[dsp]") {
  constexpr size_t B = 64;
  WaterfallProcessor wf(B);
  wf.db_floor = -120.0f;
  wf.db_ceil = -20.0f;

  std::vector<float> flat(B, -80.0f);
  std::vector<uint8_t> row(B);
  for (int i = 0; i < 10; ++i) wf.process(flat.data(), B, row.data());

  // 平坦输入 → 行字节接近恒定且在映射范围内
  for (size_t i = 0; i < B; ++i) {
    CHECK(row[i] == row[0]);
    CHECK(row[0] == Approx(255.0f * 0.4f).epsilon(0.15));  // (-80+120)/100*255
  }

  // 突发强信号：EMA 抬升、峰值 ≥ 当前值
  std::vector<float> burst(B, -80.0f);
  burst[7] = -20.0f;
  wf.process(burst.data(), B, row.data());
  CHECK(wf.peakHoldDb()[7] >= wf.emaDb()[7]);
  CHECK(row[7] > row[8]);

  // 峰值按帧衰减
  const float p0 = wf.peakHoldDb()[7];
  wf.process(flat.data(), B, row.data());
  wf.process(flat.data(), B, row.data());
  CHECK(wf.peakHoldDb()[7] < p0);
}

TEST_CASE("dsp: synth silence is only bounded noise", "[dsp]") {
  constexpr size_t N = 512;
  SignalSynth synth(N, 48'000.0);
  synth.setTones({});
  synth.setNoiseFloor(1e-3f);
  std::vector<float> buf(N);
  synth.fill(buf.data());
  float peak = 0.0f;
  for (float v : buf) peak = std::max(peak, std::abs(v));
  CHECK(peak <= 4e-3f);
  CHECK(peak > 0.0f);
}

namespace {
constexpr double kPiDsp = 3.14159265358979323846;

// bin 对准的单音（无泄漏），幅度 amp：功率精确落在 kBin 上，便于算 dB。
void fillBinTone(std::vector<float>& buf, size_t bin, double amp) {
  const size_t n = buf.size();
  for (size_t i = 0; i < n; ++i)
    buf[i] = static_cast<float>(
        amp * std::cos(2.0 * kPiDsp * static_cast<double>(bin * i) /
                       static_cast<double>(n)));
}
}  // namespace

TEST_CASE("dsp: power averaging converges without shifting tone level",
          "[dsp]") {
  constexpr size_t N = 256;
  constexpr size_t kBin = 32;
  std::vector<float> loud(N), quiet(N);
  fillBinTone(loud, kBin, 1.0);
  fillBinTone(quiet, kBin, 1.0 / std::sqrt(2.0));  // 功率减半 = -3.01 dB

  SpectrumAnalyzer plain(N, 1'000.0);
  plain.process(loud.data(), N);
  const float loud_db = plain.powerDb()[kBin];

  SpectrumAnalyzer an(N, 1'000.0);
  an.setAveragingFrames(2);
  CHECK(an.averagingFrames() == 2);
  an.process(loud.data(), N);
  // 首帧直接作种子：平均态不夹带历史，显示与未平均一致
  CHECK(an.powerDb()[kBin] == Approx(loud_db).margin(1e-3));

  // 递归平均（α=1/2）收敛到半功率，而不是继续下沉
  for (int i = 0; i < 12; ++i) an.process(quiet.data(), N);
  CHECK(loud_db - an.powerDb()[kBin] == Approx(3.0103).margin(0.02));

  // 关掉平均立刻回到单帧口径（setAveragingFrames 会清种子）
  an.setAveragingFrames(1);
  an.process(quiet.data(), N);
  CHECK(an.powerDb()[kBin] == Approx(loud_db - 3.0103).margin(0.02));
}

TEST_CASE("dsp: smeter mapping matches IARU scale", "[dsp]") {
  using namespace radio::app;
  CHECK(smeterDbmToNorm(kSmeterS9Dbm) == Approx(kSmeterS9Norm).margin(1e-6));
  CHECK(smeterDbmToNorm(kSmeterS1Dbm) == Approx(0.0f).margin(1e-6));
  CHECK(smeterDbmToNorm(kSmeterPlus60Dbm) == Approx(1.0f).margin(1e-6));
  // S9 以上 60 dB 占后 1/3 弧：10 dB 只占总量 (10/60)×(1/3)
  CHECK(smeterDbmToNorm(-63.0f) ==
        Approx(kSmeterS9Norm + (10.0f / 60.0f) * (1.0f - kSmeterS9Norm))
            .margin(1e-6));

  CHECK(smeterUnits(-91.0f) == Approx(6.0f).margin(1e-6));
  CHECK(smeterUnits(-127.0f) == Approx(0.0f).margin(1e-6));  // 钳到 S0
  CHECK(smeterUnits(-13.0f) == Approx(9.0f).margin(1e-6));   // 钳到 S9

  for (float dbm : {-121.0f, -109.0f, -85.0f, -73.0f, -43.0f, -13.0f})
    CHECK(smeterNormToDbm(smeterDbmToNorm(dbm)) == Approx(dbm).margin(1e-4));

  // 单调不减：dBm 升高时表针不回头
  float prev = -1.0f;
  for (float dbm = -130.0f; dbm <= 0.0f; dbm += 0.5f) {
    const float norm = smeterDbmToNorm(dbm);
    CHECK(norm >= prev);
    prev = norm;
  }
}
