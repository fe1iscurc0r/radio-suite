// SPDX-License-Identifier: AGPL-3.0-or-later
// 频段白名单测试（TEST_CASE 名保持 ASCII，理由见 test_civ.cpp 头注）
#include <catch2/catch_test_macros.hpp>

#include "core/bands.h"

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
