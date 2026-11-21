// SPDX-License-Identifier: AGPL-3.0-or-later
// radio-suite · tests/test_satellite_tracker.cpp — W2.4 跟踪执行者（时钟注入，无 GUI）
//
// 验的是「执行者」而不是「策略」：策略的死区/间隔/发射中拒写由 test_track_policy 锁；
// 这里锁的是接线后的行为——同一毫秒不重复写、真移动了才写、发射中一律不写、
// LOS 自动停（而且「过境前等待」不能被误判成 LOS）。
#include <catch2/catch_test_macros.hpp>

#include <libsgp4/DateTime.h>
#include <libsgp4/Tle.h>

#include "app/satellite_tracker.h"
#include "qt_test_app.h"

#include <cmath>
#include <vector>

namespace {

// 2026-11-20 21:09:47 UTC 历元；北京站（39.9/116.4/0.05）当天 06:35–06:44 有一次过境，
// TCA 06:39:59 仰角 44.8°（过境参数另见 evidence/2026-11-21-satpass-crosscheck/）。
constexpr const char* kIssL1 =
    "1 25544U 98067A   26324.88180268  .00009897  00000+0  18414-3 0  9991";
constexpr const char* kIssL2 =
    "2 25544  51.6319 244.6605 0007389  48.0812 312.0806 15.49518511596048";
constexpr double kNominalHz = 145800000.0;  // 2m 下行标称，随便取但要固定

struct Rig {
  std::vector<double> writes;
  bool tx = false;
  libsgp4::DateTime now{2026, 11, 21, 6, 40, 0};
};

struct Fixture {
  Rig rig;
  QString stopped_reason;
  radio::app::SatelliteTracker tracker{
      [this](double hz) { rig.writes.push_back(hz); },
      [this] { return rig.tx; },
      [this] { return rig.now; }};
  libsgp4::Tle tle{kIssL1, kIssL2};
  radio::satellite::Station st{39.9, 116.4, 0.05};
};

}  // namespace

TEST_CASE("tracker: same tick throttles, moving target writes once", "[satellite][track]") {
  qtTestApp();
  Fixture f;
  f.tracker.start(f.tle, f.st, kNominalHz, 5.0);

  // start() 立刻一拍：目标与 0 相差巨大 → 第一笔写
  REQUIRE(f.rig.writes.size() == 1);
  CHECK(f.tracker.active());
  CHECK(f.tracker.snapshot().above_horizon);
  const double first = f.rig.writes.front();

  // 同一毫秒再一拍：最小间隔挡掉，不许重复写
  f.tracker.tick();
  CHECK(f.rig.writes.size() == 1);

  // 3 分钟后（仍在过境里，接近 LOS，ṙ 很大）：目标移动远超死区 → 第二笔写
  f.rig.now = libsgp4::DateTime(2026, 11, 21, 6, 43, 0);
  f.tracker.tick();
  REQUIRE(f.rig.writes.size() == 2);
  CHECK(std::abs(f.rig.writes[1] - first) > 200.0);  // 死区 200 Hz
  CHECK(f.tracker.snapshot().writes == 2);
}

TEST_CASE("tracker: transmitting blocks every write", "[satellite][track]") {
  qtTestApp();
  Fixture f;
  f.rig.tx = true;
  f.tracker.start(f.tle, f.st, kNominalHz, 5.0);
  CHECK(f.rig.writes.empty());  // 开跟踪那一拍就在发射 → 一笔也不许写

  f.rig.now = libsgp4::DateTime(2026, 11, 21, 6, 43, 0);
  f.tracker.tick();
  CHECK(f.rig.writes.empty());

  f.rig.tx = false;
  f.tracker.tick();
  CHECK(f.rig.writes.size() == 1);  // 一旦回到 RX，下一拍才补写
}

TEST_CASE("tracker: LOS stops tracking and says why", "[satellite][track]") {
  qtTestApp();
  Fixture f;
  QObject::connect(&f.tracker, &radio::app::SatelliteTracker::trackingStopped,
                   &f.tracker, [&f](const QString& r) { f.stopped_reason = r; });
  f.tracker.start(f.tle, f.st, kNominalHz, 5.0);
  REQUIRE(f.tracker.active());

  // 06:50 早已过了 LOS（06:44:07）
  f.rig.now = libsgp4::DateTime(2026, 11, 21, 6, 50, 0);
  f.tracker.tick();
  CHECK_FALSE(f.tracker.active());
  CHECK(f.stopped_reason.contains("LOS"));
}

TEST_CASE("tracker: pre-AOS wait is not a LOS", "[satellite][track]") {
  qtTestApp();
  Fixture f;
  f.rig.now = libsgp4::DateTime(2026, 11, 21, 6, 30, 0);  // AOS 之前
  f.tracker.start(f.tle, f.st, kNominalHz, 5.0);
  CHECK(f.tracker.active());
  CHECK_FALSE(f.tracker.snapshot().above_horizon);
  CHECK(f.rig.writes.empty());  // 没上过天：等待，不许写

  f.rig.now = libsgp4::DateTime(2026, 11, 21, 6, 36, 30);  // AOS 之后
  f.tracker.tick();
  CHECK(f.tracker.active());
  CHECK(f.tracker.snapshot().above_horizon);
  CHECK(f.rig.writes.size() == 1);
}
