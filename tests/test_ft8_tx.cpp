// SPDX-License-Identifier: AGPL-3.0-or-later
// FT8 本机发射：时隙排期算术 + 整槽波形渲染 + 渲染→本机解调闭环
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

#include "ft8/ft8_slot.h"
#include "ft8/ft8_tx.h"

using namespace radio::ft8;
using Catch::Approx;

TEST_CASE("ft8 tx: next slot start honors lead time", "[ft8][tx]") {
  // 槽起点是 15 s 的整数倍；早于「now + lead」的边界一律不许排
  CHECK(nextTxSlotStart(0, 0, -1) == 0);
  CHECK(nextTxSlotStart(0, 500, -1) == 15'000);
  CHECK(nextTxSlotStart(15'000, 500, -1) == 30'000);  // 已开始的那槽不能排
  CHECK(nextTxSlotStart(14'500, 500, -1) == 15'000);  // 刚好够提前量
  CHECK(nextTxSlotStart(14'600, 500, -1) == 30'000);  // 差 100ms 不够键控
  CHECK(nextTxSlotStart(86'400'000 + 7'500, 500, -1) == 86'400'000 + 15'000);
  CHECK(nextTxSlotStart(20'000, -1, -1) == 30'000);  // 负的提前量当 0
}

TEST_CASE("ft8 tx: parity picks even or odd slot numbers", "[ft8][tx]") {
  // 偶序号槽落在每分钟 :00 与 :30（FT8 约定：偶/奇时隙交替发射）
  CHECK(nextTxSlotStart(0, 500, 0) == 30'000);
  CHECK(nextTxSlotStart(0, 500, 1) == 15'000);
  CHECK(nextTxSlotStart(31'000, 500, 0) == 60'000);
  CHECK(nextTxSlotStart(31'000, 500, 1) == 45'000);
  for (int parity : {0, 1}) {
    for (int64_t now = 0; now < 600'000; now += 7'000) {
      const int64_t start = nextTxSlotStart(now, 500, parity);
      CHECK(start >= now + 500);
      CHECK(start % kTxSlotMs == 0);
      CHECK((start / kTxSlotMs) % 2 == parity);
      CHECK(start - now <= 2 * kTxSlotMs);  // 最多多等一槽
    }
  }
}

TEST_CASE("ft8 tx: rendered pcm is exactly one 48k slot", "[ft8][tx]") {
  const auto pcm = renderTxPcm("CQ BG5GXO PM01", 1000.0f);
  REQUIRE(pcm.size() == static_cast<size_t>(kTxSlotMs / 1000) * 48'000);
  int peak = 0;
  for (int16_t s : pcm)
    peak = std::max(peak, std::abs(static_cast<int>(s)));
  INFO("peak = " << peak);
  CHECK(peak > 18'000);    // 默认 0.7 满量程：太小说明标度写错
  CHECK(peak <= 32'767);   // 不许溢出
  CHECK(std::abs(static_cast<int>(pcm.front())) < 3'000);  // 槽首留静音保护
  CHECK_THROWS_AS(renderTxPcm("this text is far too long for ft8", 1000.0f),
                  std::runtime_error);
}

TEST_CASE("ft8 tx: rendered slot decodes back through the local rx chain",
          "[ft8][tx]") {
  // 发射侧渲染的整槽 PCM，直接喂本机槽解码器（48k→12k 那条路）应能解回原文：
  // 上采样与满量程标度和接收侧对得上，才谈得上「自己发的自己能解」
  const std::string kText = "CQ BG5GXO PM01";
  const auto pcm = renderTxPcm(kText, 1500.0f);

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
  for (size_t i = 0; i < pcm.size(); i += 960)
    dec.push(pcm.data() + i, std::min<size_t>(960, pcm.size() - i));
  {
    std::unique_lock<std::mutex> lk(mu);
    REQUIRE(cv.wait_for(lk, std::chrono::seconds(120), [&] { return done; }));
  }
  dec.stop();

  const DecodedMessage* hit = nullptr;
  for (const auto& m : got.messages)
    if (m.text == kText) hit = &m;
  INFO("decoded " << got.messages.size() << " messages, first = "
                  << (got.messages.empty() ? std::string("<none>")
                                           : got.messages.front().text));
  REQUIRE(hit != nullptr);
  CHECK(hit->df_hz == Approx(1500.0f).margin(25.0f));
  CHECK(hit->snr_db > 0.0f);
  CHECK(dec.droppedSlots() == 0);
}
