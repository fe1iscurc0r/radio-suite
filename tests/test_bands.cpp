// SPDX-License-Identifier: AGPL-3.0-or-later
// 频段白名单测试（TEST_CASE 名保持 ASCII，理由见 test_civ.cpp 头注）
#include <catch2/catch_test_macros.hpp>

#include "core/bands.h"
#include "core/safety.h"

using namespace radio::core;

TEST_CASE("bands: in-band allowed", "[bands]") {
  CHECK(isAllowedFreq(1'800'000));      // 160m 下边
  CHECK(isAllowedFreq(3'550'000));      // 80m
  CHECK(isAllowedFreq(7'100'000));      // 40m
  CHECK(isAllowedFreq(14'270'000));     // 20m
  CHECK(isAllowedFreq(29'700'000));     // 10m 上边
  CHECK(isAllowedFreq(50'100'000));     // 6m
  CHECK(isAllowedFreq(145'500'000));    // 2m（参考实现白名单段）
  CHECK(isAllowedFreq(148'000'000));    // 2m 上边（闭区间）
  CHECK(isAllowedFreq(432'000'000));    // 70cm
  CHECK(isAllowedFreq(1'296'000'000));  // 23cm
}

TEST_CASE("bands: out-of-band rejected", "[bands]") {
  CHECK_FALSE(isAllowedFreq(0));
  CHECK_FALSE(isAllowedFreq(1'000'000));      // 160m 以下
  CHECK_FALSE(isAllowedFreq(2'100'000));      // 160m/80m 之间
  CHECK_FALSE(isAllowedFreq(31'000'000));     // HF 与 6m 之间
  CHECK_FALSE(isAllowedFreq(88'000'000));     // FM 广播
  CHECK_FALSE(isAllowedFreq(149'000'000));    // 2m 以上（航空/卫星）
  CHECK_FALSE(isAllowedFreq(420'000'000));    // 70cm 以下
  CHECK_FALSE(isAllowedFreq(2'400'000'000));  // 2.4G ISM
}

TEST_CASE("bands: bandFor lookup", "[bands]") {
  auto b = bandFor(14'270'000);
  REQUIRE(b.has_value());
  CHECK(std::string(b->name) == "20m");
  CHECK(b->lo_hz == 14'000'000);
  CHECK(b->hi_hz == 14'350'000);
  CHECK_FALSE(bandFor(100'000'000).has_value());
}

// RX/TX 策略拆分：广播/航空收听开放，发射仍锁业余白名单
// RX 范围对齐原厂 IC-705.ini [FREQUENCY]：30kHz–200MHz + 400–470MHz
TEST_CASE("bands: RX open range vs TX whitelist", "[bands]") {
  CHECK(isRxAllowed(121'500'000));    // 航空波段：可收听
  CHECK(isRxAllowed(98'000'000));     // 调频广播：可收听
  CHECK(isRxAllowed(7'100'000));      // 业余段两者皆可
  CHECK(isRxAllowed(30'000));         // 原厂下界
  CHECK(isRxAllowed(199'999'999));    // 段一上界
  CHECK(isRxAllowed(400'000'000));    // 段二下界
  CHECK(isRxAllowed(433'000'000));    // 70cm
  CHECK(isRxAllowed(470'000'000));    // 段二上界
  CHECK_FALSE(isRxAllowed(29'999));   // 下界外
  CHECK_FALSE(isRxAllowed(350'000'000));  // 200–400MHz 空洞（原厂不支持）
  CHECK_FALSE(isRxAllowed(1'240'000'000)); // IC-705 无 1.2G 段
  CHECK_FALSE(isRxAllowed(2'400'000'000));

  CHECK_FALSE(isTxAllowed(121'500'000));  // 航空：禁止发射
  CHECK_FALSE(isTxAllowed(98'000'000));   // 广播：禁止发射
  CHECK(isTxAllowed(14'270'000));         // 业余段：允许发射
  CHECK(SafetyGate::authorizeTx(145'500'000));
  CHECK_FALSE(SafetyGate::authorizeTx(88'000'000));
}
