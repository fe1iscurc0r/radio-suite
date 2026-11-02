// SPDX-License-Identifier: AGPL-3.0-or-later
// 本机声学环回（默认跳过）：扬声器按 UTC 槽起点播一个合成 FT8 时隙，麦克风同时
// 采集并喂给同一个槽解码器——不接电台、不接 50003，只用房间里的声音。
// 要真出声、要真麦克风，故只在显式设了 RADIO_SUITE_FT8_ACOUSTIC=1 时才跑。
#include <catch2/catch_test_macros.hpp>

#include <QByteArray>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "audio/audio_in.h"
#include "audio/audio_out.h"
#include "ft8/ft8_slot.h"
#include "ft8/ft8_tx.h"

namespace {

int64_t utcMsNow() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

bool enabled() {
  static const bool on = !qgetenv("RADIO_SUITE_FT8_ACOUSTIC").isEmpty();
  return on;
}

}  // namespace

TEST_CASE("ft8 acoustic loopback: speaker plays a slot and microphone decodes it",
          "[ft8][acoustic]") {
  if (!enabled()) {
    WARN("未设 RADIO_SUITE_FT8_ACOUSTIC：跳过声学环回（要真扬声器 + 真麦克风）");
    return;
  }

  const std::string kText = "CQ BG5GXO PM01";
  // 峰值压到 0.35：这是房间里的声学耦合，不是电台满驱动，不必把邻居吵起来
  const auto pcm = radio::ft8::renderTxPcm(kText, 1500.0f, 0.35f);
  REQUIRE(pcm.size() ==
          static_cast<size_t>(radio::ft8::kTxSlotMs) * 48);  // 15 s @ 48 kHz

  radio::audio::AudioIn in;
  radio::audio::AudioOut out;
  const int in_dev = in.defaultInputDevice();
  const int out_dev = out.defaultOutputDevice();
  REQUIRE(in_dev >= 0);
  REQUIRE(out_dev >= 0);

  radio::ft8::SlotDecoder dec(48000);
  std::mutex mu;
  std::condition_variable cv;
  bool done = false;
  radio::ft8::SlotResult got;
  dec.setCallback([&](const radio::ft8::SlotResult& r) {
    std::lock_guard<std::mutex> lk(mu);
    got = r;
    done = true;
    cv.notify_all();
  });

  std::atomic<size_t> captured{0};
  std::atomic<size_t> peak{0};
  std::string err;
  // 采集先开：解码器对齐等待期里的样本只计数不入槽，声卡启动的几十毫秒落在导引段
  REQUIRE(in.start(
      static_cast<uint32_t>(in_dev),
      [&](std::span<const int16_t> frame) {
        captured += frame.size();
        for (int16_t s : frame) {
          const auto a = static_cast<size_t>(s < 0 ? -s : s);
          size_t expect = peak.load(std::memory_order_relaxed);
          while (a > expect &&
                 !peak.compare_exchange_weak(expect, a, std::memory_order_relaxed)) {
          }
        }
        dec.push(frame);
      },
      &err));

  const int64_t wait_ms = dec.start(utcMsNow());
  if (wait_ms > 0)
    std::this_thread::sleep_for(std::chrono::milliseconds(wait_ms));

  const bool played =
      out.play(pcm, 48000, static_cast<uint32_t>(out_dev), &err);

  bool got_slot = false;
  {
    // play() 阻塞到整槽放完；采集线程还在喂，最后那半个符号要靠这里补齐
    std::unique_lock<std::mutex> lk(mu);
    got_slot = cv.wait_for(lk, std::chrono::seconds(25), [&] { return done; });
  }
  in.stop();
  dec.stop();

  INFO("captured=" << captured.load() << " peak=" << peak.load()
                   << " played=" << played << " err=" << err);
  REQUIRE(played);
  REQUIRE(captured.load() >= static_cast<size_t>(48000) * 14);
  // 喇叭和麦克风不耦合的话，这里会是个位数十几量级：先证明「听见了」再谈解码
  REQUIRE(peak.load() > 300);

  REQUIRE(got_slot);
  bool found = false;
  const radio::ft8::DecodedMessage* hit = nullptr;
  for (const auto& d : got.messages) {
    if (d.text == kText) {
      found = true;
      hit = &d;
    }
  }
  INFO("captured=" << captured.load() << " peak=" << peak.load()
                   << " messages=" << got.messages.size()
                   << " snr_db=" << (hit ? hit->snr_db : 0.0f)
                   << " dt_s=" << (hit ? hit->dt_s : 0.0f)
                   << " df_hz=" << (hit ? hit->df_hz : 0.0f)
                   << " decode_ms=" << got.decode_ms);
  REQUIRE(found);
}
