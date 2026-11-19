// SPDX-License-Identifier: AGPL-3.0-or-later
// radio-suite · tests/test_satellite.cpp
// 过境几何与多普勒的验收。判据全部来自独立物理，不拿实现自己配自己：
// 半长轴用 TLE 的平均运动经开普勒第三定律算，轨道速度用 2πa/P 算，
// 再看 look angle 给出的斜距/仰角/距离变化率能不能与它们闭合。
//
// 为什么这样判：rad/deg 混掉、方位角象限补错、距离变化率符号反了，
// 这三类错都会让「闭合关系」崩掉，而任何「跑一遍没崩」式的用例都看不见它们。
//
// 地球半径与 μ 刻意取 libsgp4 Globals.h 的同源值（6378.135 / 398600.8，WGS72）：
// 三角形要闭合就必须用同一个地球，否则测出来的偏差里混着常量差，判不出对错。

#include "satellite/sat_track.h"

#include <libsgp4/SatelliteException.h>
#include <libsgp4/Tle.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace {

// 官方向量文件里的 DELTA 1 DEB：近圆（e=0.0030035）、倾角 58.06°，适合做几何闭合检查。
const char kLine1[] =
    "1 06251U 62025E   06176.82412014  .00008885  00000-0  12808-3 0  3985";
const char kLine2[] =
    "2 06251  58.0579  54.0425 0030035 139.1568 221.1854 15.56387291  6774";
constexpr double kMeanMotionRevDay = 15.56387291;
constexpr double kEcc = 0.0030035;

constexpr double kMuKm3S2 = 398600.8;    // 与 libsgp4 Globals 同源
constexpr double kReKm = 6378.135;       // 与 libsgp4 kXKMPER 同源
constexpr double kC = 299792458.0;

constexpr double kPi = 3.14159265358979323846;

// 北京：中纬度、有昼夜两侧可见的过境，够典型。
const radio::satellite::Station kBeijing{39.9, 116.4, 0.05};

double semi_major_km() {
  const double n = kMeanMotionRevDay * 2.0 * kPi / 86400.0;  // rad/s
  return std::cbrt(kMuKm3S2 / (n * n));
}

double circular_speed_kms() {
  const double a = semi_major_km();
  const double period_s = 86400.0 / kMeanMotionRevDay;
  return 2.0 * kPi * a / period_s;
}

// 由斜距 ρ 与仰角 el 反解卫星高度：地面站–地心–卫星三角，
// (Re+h)^2 = Re^2 + ρ^2 + 2·Re·ρ·sin(el)。
double altitude_from_geometry(double range_km, double el_deg) {
  const double s = std::sin(el_deg * kPi / 180.0);
  return std::sqrt(kReKm * kReKm + range_km * range_km +
                   2.0 * kReKm * range_km * s) -
         kReKm;
}

radio::satellite::Pass highest_pass(const libsgp4::Tle& tle,
                                    double from,
                                    double to) {
  const auto passes = radio::satellite::find_passes(tle, kBeijing, from, to, 5.0);
  REQUIRE_FALSE(passes.empty());
  const auto best = std::max_element(passes.begin(), passes.end(),
                                     [](const auto& a, const auto& b) {
                                       return a.max_el_deg < b.max_el_deg;
                                     });
  return *best;
}

}  // namespace

TEST_CASE("satellite: doppler sign, magnitude and the doubled uplink correction",
          "[satellite][doppler]") {
  // 远离（ṙ>0）⇒ 收到的频率降低。符号搞反是最贵的一类错：跟频会越跟越远。
  CHECK(radio::satellite::doppler_hz(145.9e6, +7.0) < 0.0);
  CHECK(radio::satellite::doppler_hz(145.9e6, -7.0) > 0.0);

  // 量级独立算：Δf = -v_r/c · f = -7000/299792458 · 145.9e6 = -3406.69 Hz
  CHECK(radio::satellite::doppler_hz(145.9e6, 7.0) ==
        Catch::Approx(-3406.690104).epsilon(1e-6));

  // 线性转发器上行要补两倍：比值 2.0000233，一阶就是 2。
  const double rr = 7.0;
  const double down_shift =
      radio::satellite::doppler_hz(435.9e6, rr);  // 下行单程偏移（负）
  const double up = radio::satellite::linear_uplink_hz(145.9e6, rr);
  const double up_shift = up - 145.9e6;
  // 远离时上行要抬（正），而下行的接收偏移是负的：两边都反号才对得上物理。
  CHECK(up_shift > 0.0);
  CHECK(down_shift < 0.0);
  CHECK(up_shift / std::abs(down_shift) ==
        Catch::Approx((145.9e6 / 435.9e6) * 2.0).epsilon(1e-4));
}

TEST_CASE("satellite: pass geometry closes against Kepler's third law",
          "[satellite][geometry]") {
  const libsgp4::Tle tle(kLine1, kLine2);
  const auto p = highest_pass(tle, 0.0, 1440.0);
  CAPTURE(p.max_el_deg, p.min_range_km);

  // 仰角必须是真的「几十度」而不是「零点几弧度」——rad/deg 混掉时这条先炸。
  REQUIRE(p.max_el_deg > 10.0);

  const radio::satellite::Look at_tca = radio::satellite::look_at(tle, p.tca_min, kBeijing);
  const double h = altitude_from_geometry(at_tca.range_km, at_tca.el_deg);
  const double a = semi_major_km();
  // 近圆轨道的真实地心距落在 a(1±e) 之间，即高度 377.77–418.48 km。
  // 再加 15 km 容忍把地球扁率与 TCA 处的真近点角余量算进去。
  const double perigee_alt = a * (1.0 - kEcc) - kReKm;
  const double apogee_alt = a * (1.0 + kEcc) - kReKm;
  INFO("Kepler: a = " << a << " km, perigee " << perigee_alt << " km, apogee "
                      << apogee_alt << " km; geometry gives h = " << h << " km");
  CHECK(h > perigee_alt - 15.0);
  CHECK(h < apogee_alt + 15.0);

  // 方位角必须落在 [0,360)，补象限错了会出现负值或 >360。
  CHECK(at_tca.az_deg >= 0.0);
  CHECK(at_tca.az_deg < 360.0);
}

TEST_CASE("satellite: range rate crosses zero at closest approach",
          "[satellite][geometry][doppler]") {
  const libsgp4::Tle tle(kLine1, kLine2);
  const auto p = highest_pass(tle, 0.0, 1440.0);

  const double rr_aos = radio::satellite::look_at(tle, p.aos_min, kBeijing).range_rate_kms;
  const double rr_tca = radio::satellite::look_at(tle, p.tca_min, kBeijing).range_rate_kms;
  const double rr_los = radio::satellite::look_at(tle, p.los_min, kBeijing).range_rate_kms;
  CAPTURE(rr_aos, rr_tca, rr_los);

  // 任何一次过境都必然是「先接近、后远离」：两端反号，且中间过零。
  REQUIRE(rr_aos < 0.0);
  REQUIRE(rr_los > 0.0);

  // 最近点处径向速度接近 0（严格为 0 只在几何星下点正上方，实际是局部极小）。
  CHECK(std::abs(rr_tca) < 0.2 * std::abs(rr_los));

  // 上界：径向速度不可能超过轨道速度本身。v = 2πa/P 独立算出来 7.67 km/s。
  const double v = circular_speed_kms();
  INFO("circular speed from Kepler = " << v << " km/s");
  CHECK(std::abs(rr_aos) < v * 1.02);
  CHECK(std::abs(rr_los) < v * 1.02);
  // 下界同样要判：如果实现坏了恒返回 0，上面几条会全过而这里必挂。
  CHECK(std::abs(rr_los) > 0.5 * v);
}

TEST_CASE("satellite: pass windows bracket the elevation floor",
          "[satellite][passes]") {
  const libsgp4::Tle tle(kLine1, kLine2);
  const auto passes = radio::satellite::find_passes(tle, kBeijing, 0.0, 1440.0, 5.0);

  // 24 小时里一个 92.5 min 周期的 LEO 绕 ~15.6 圈；中纬度站可见次数落在 4–8。
  // 这是物理区间，不是实测凑数：n 圈里大约一半时间轨道在站地平线以上。
  INFO("passes in 24 h = " << passes.size());
  CHECK(passes.size() >= 4);
  CHECK(passes.size() <= 8);

  for (const auto& p : passes) {
    CAPTURE(p.aos_min, p.tca_min, p.los_min);
    CHECK(p.aos_min < p.tca_min);
    CHECK(p.tca_min < p.los_min);
    // 92.5 min 周期的过境，仰角 >5° 的窗口不可能超过 30 min。
    CHECK(p.los_min - p.aos_min < 30.0);
    // 二分收敛：边界上仰角就该压在地平阈值上。
    CHECK(radio::satellite::look_at(tle, p.aos_min, kBeijing).el_deg ==
          Catch::Approx(5.0).margin(0.02));
    CHECK(radio::satellite::look_at(tle, p.los_min, kBeijing).el_deg ==
          Catch::Approx(5.0).margin(0.02));
  }

  // 空区间不该抛也不该编出一个过境。
  CHECK(radio::satellite::find_passes(tle, kBeijing, 100.0, 100.0, 5.0).empty());
}

// ---- W2.1 时基（WO-275 §5.2）----
// 判据来自 libsgp4 自己的历元数学：tsince = (now − epoch) 分钟。
// 0.1 分钟量化是 TLE 数据特性，写成量级说明，不当 bug 修。
TEST_CASE("satellite: tsince time base", "[satellite]") {
  const libsgp4::Tle tle(kLine1, kLine2);
  using libsgp4::TimeSpan;

  // 历元前 10 分钟 → 负 tsince（反向传播属正常值）
  const auto epoch = tle.Epoch();
  CHECK(radio::satellite::tsince_min(tle, epoch - TimeSpan(0, 0, 10, 0)) ==
        Catch::Approx(-10.0).margin(0.01));

  // 跨年：2006-06-25 历元 → 2007-01-02，约 190 天量级
  const libsgp4::DateTime y2007(2007, 1, 2, 0, 0, 0);
  const double cross = radio::satellite::tsince_min(tle, y2007);
  CHECK(cross > 180.0 * 24 * 60);
  CHECK(cross < 200.0 * 24 * 60);

  // D-2：两天后的 tsince ≈ 2880 min（容差含 0.1 min 量化）
  const auto d2 = epoch + TimeSpan(2, 0, 0, 0);
  CHECK(radio::satellite::tsince_min(tle, d2) ==
        Catch::Approx(2880.0).margin(0.11));
}

// ---- A-2.2 金值回归：站坐标一换，az/el/range/AOS/LOS 全变——专门堵
// 「过境边界自洽性检查对站坐标互换全盲」这个洞（变异实验实测存活过）。
// 金值由未变异实现在 t=1000 与 [1000,2000] 区间一次生成，变更需显式重录。
TEST_CASE("satellite: beijing golden pins", "[satellite]") {
  const libsgp4::Tle tle(kLine1, kLine2);
  const radio::satellite::Station beijing{39.9, 116.4, 0.05};
  const double t = 1000.0;
  const auto look = radio::satellite::look_at(tle, t, beijing);
  CHECK(look.az_deg == Catch::Approx(126.508864).margin(0.01));
  CHECK(look.el_deg == Catch::Approx(-75.173350).margin(0.01));
  CHECK(look.range_km == Catch::Approx(12752.385256).margin(0.01));
  CHECK(look.range_rate_kms == Catch::Approx(1.639376).margin(0.01));

  const auto passes =
      radio::satellite::find_passes(tle, beijing, 1000.0, 2000.0, 5.0);
  REQUIRE(passes.size() == 2);
  CHECK(passes[0].aos_min == Catch::Approx(1857.685306).margin(0.01));
  CHECK(passes[0].tca_min == Catch::Approx(1861.697442).margin(0.01));
  CHECK(passes[0].los_min == Catch::Approx(1865.671675).margin(0.01));
  CHECK(passes[0].max_el_deg == Catch::Approx(42.947826).margin(0.01));
  CHECK(passes[1].aos_min == Catch::Approx(1954.255501).margin(0.01));
  CHECK(passes[1].tca_min == Catch::Approx(1957.621868).margin(0.01));
  CHECK(passes[1].los_min == Catch::Approx(1960.962965).margin(0.01));
  CHECK(passes[1].max_el_deg == Catch::Approx(16.572445).margin(0.01));
}
