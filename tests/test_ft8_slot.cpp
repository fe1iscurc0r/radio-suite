// SPDX-License-Identifier: AGPL-3.0-or-later
// FT8 槽解码器：UTC 对齐 / 48k→12k 抽取 / 合成信号端到端 / stop 清理
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

#include "ft8/ft8_slot.h"

using namespace radio::ft8;

namespace {

// 12k float → 48k s16（零阶保持；抽取回 12k 后与原始样本一致）
std::vector<int16_t> up4x(const std::vector<float>& audio12k) {
  std::vector<int16_t> pcm;
  pcm.reserve(audio12k.size() * 4);
  for (float v : audio12k) {
    const auto s = static_cast<int16_t>(std::lround(v * 12000.0f));
    for (int k = 0; k < 4; ++k) pcm.push_back(s);
  }
  return pcm;
}

}  // namespace

TEST_CASE("ft8 slot decoder: start aligns to the next 15s boundary",
          "[ft8][slot]") {
  SlotDecoder dec(48000);
  REQUIRE(dec.start(0) == 0);          // 已在边界上：立刻开始
  REQUIRE(dec.start(1) == 14'999);     // 差 1ms：等 14.999s
  REQUIRE(dec.start(14'999) == 1);
  REQUIRE(dec.start(15'000) == 0);
  REQUIRE(dec.start(86'400'000 + 7'500) == 7'500);  // 一天整 + 半槽
  dec.stop();
  REQUIRE_FALSE(dec.running());
}

TEST_CASE("ft8 slot decoder: decodes a synthesized slot fed as 48k pcm",
          "[ft8][slot]") {
  const auto tones = encodeTones("CQ BG5GXO PM01");
  const auto audio = synthesize(tones, 1000.0f);
  REQUIRE(audio.size() == kSlotSamples12k);
  const auto pcm = up4x(audio);

  SlotDecoder dec(48000);
  std::mutex mu;
  std::condition_variable cv;
  SlotResult got;
  bool done = false;
  dec.setCallback([&](const SlotResult& r) {
    std::lock_guard<std::mutex> lk(mu);
    got = r;
    done = true;
    cv.notify_all();
  });

  REQUIRE(dec.start(0) == 0);
  // 按 20ms 块喂（与 50003 下行节奏一致）
  for (size_t i = 0; i < pcm.size(); i += 960)
    dec.push(pcm.data() + i, std::min<size_t>(960, pcm.size() - i));

  {
    std::unique_lock<std::mutex> lk(mu);
    REQUIRE(cv.wait_for(lk, std::chrono::seconds(120), [&] { return done; }));
  }
  bool found = false;
  for (const auto& d : got.messages)
    if (d.text == "CQ BG5GXO PM01") found = true;
  REQUIRE(found);
  REQUIRE(got.utc_ms == 0);
  REQUIRE(dec.slotsDecoded() == 1);
  REQUIRE(dec.droppedSlots() == 0);
  dec.stop();
}

TEST_CASE("ft8 slot decoder: stop drops the partial slot", "[ft8][slot]") {
  SlotDecoder dec(48000);
  int calls = 0;
  dec.setCallback([&](const SlotResult&) { ++calls; });

  REQUIRE(dec.start(0) == 0);
  std::vector<int16_t> chunk(960, 0);
  for (int i = 0; i < 100; ++i)  // 2s 噪声，不足一槽
    dec.push(chunk.data(), chunk.size());
  REQUIRE(dec.slotProgress() > 0.1);
  REQUIRE(dec.slotProgress() < 0.2);
  REQUIRE(dec.fedSamples() == 100 * 960);

  dec.stop();
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  REQUIRE(calls == 0);
  REQUIRE(dec.slotsDecoded() == 0);
  REQUIRE(dec.slotProgress() == 0.0);
}

TEST_CASE("ft8 slot decoder: fed samples count during the aligned wait",
          "[ft8][slot]") {
  SlotDecoder dec(48000);
  // 槽中段启动：前 5s 样本要丢掉，但仍然算「音频已下来」
  REQUIRE(dec.start(10'000) == 5'000);
  std::vector<int16_t> chunk(960, 0);
  for (int i = 0; i < 100; ++i) dec.push(chunk.data(), chunk.size());
  REQUIRE(dec.slotProgress() == 0.0);
  REQUIRE(dec.fedSamples() == 100 * 960);

  REQUIRE(dec.start(10'000) == 5'000);  // 重启清零
  REQUIRE(dec.fedSamples() == 0);
  dec.stop();
}
