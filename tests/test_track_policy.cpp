// SPDX-License-Identifier: AGPL-3.0-or-later
// radio-suite · tests/test_track_policy.cpp — W2.4 写频策略（纯离线）
// 判据 = 总纲 §5.5 三条硬约束逐条打表：死区不写、间隔不写、发射中不写。
#include <catch2/catch_test_macros.hpp>

#include "satellite/track_policy.h"

using radio::satellite::TrackConfig;
using radio::satellite::trackDecide;

TEST_CASE("track policy: three hard constraints", "[satellite]") {
  TrackConfig cfg;  // 死区 200Hz、最小间隔 1000ms

  // 死区内不写
  const auto d1 = trackDecide(cfg, 145'800'000.0, 145'800'000.0 + 150.0,
                              false, 5000, 0);
  CHECK_FALSE(d1.write);
  CHECK(d1.reason == "deadzone");

  // 死区外 + 首写（last=0）→ 写
  const auto d2 = trackDecide(cfg, 145'800'000.0, 145'800'000.0 + 800.0,
                              false, 5000, 0);
  CHECK(d2.write);
  CHECK(d2.rx_hz == 145'800'000.0);

  // 最小间隔未到 → 不写
  const auto d3 = trackDecide(cfg, 145'800'000.0, 145'800'000.0 + 800.0,
                              false, 5500, 5000);
  CHECK_FALSE(d3.write);
  CHECK(d3.reason == "interval");

  // 间隔已过 → 写
  const auto d4 = trackDecide(cfg, 145'800'000.0, 145'800'000.0 + 800.0,
                              false, 6100, 5000);
  CHECK(d4.write);

  // 发射中一律不写（哪怕死区外且间隔已过）
  const auto d5 = trackDecide(cfg, 145'800'000.0, 145'800'000.0 + 800.0,
                              true, 6100, 5000);
  CHECK_FALSE(d5.write);
  CHECK(d5.reason == "tx active");
}
