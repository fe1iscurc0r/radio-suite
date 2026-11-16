// SPDX-License-Identifier: AGPL-3.0-or-later
// radio-suite · satellite/sat_track.cpp
#include "satellite/sat_track.h"

#include <libsgp4/CoordTopocentric.h>
#include <libsgp4/Eci.h>
#include <libsgp4/Observer.h>
#include <libsgp4/SGP4.h>

#include <cmath>

namespace radio::satellite {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kRadToDeg = 180.0 / kPi;
// 真空光速，m/s。多普勒用经典近似 v_r/c 就够（LEO 的 v_r/c ~ 2.5e-5，
// 相对论因子在这个量级上差 1e-10，远小于 TLE 本身的误差）。
constexpr double kC = 299792458.0;

double norm360(double deg) {
  double d = std::fmod(deg, 360.0);
  if (d < 0.0) d += 360.0;
  return d;
}

// model 与 observer 都得复用：SGP4 的初始化（含深空项）不便宜，过境扫描一次要算几千点。
struct Track {
  libsgp4::SGP4 model;
  libsgp4::Observer obs;

  Track(const libsgp4::Tle& tle, const Station& st)
      : model(tle), obs(st.lat_deg, st.lon_deg, st.alt_km) {}

  Look at(double tsince_min) {
    const libsgp4::Eci eci = model.FindPosition(tsince_min);
    const libsgp4::CoordTopocentric lt = obs.GetLookAngle(eci);
    return {norm360(lt.azimuth * kRadToDeg), lt.elevation * kRadToDeg, lt.range,
            lt.rangeRate};
  }

  double el(double tsince_min) { return at(tsince_min).el_deg; }
};

// 在 [lo, hi] 内找仰角跨过 floor 的那一点。调用方保证两端一上一下。
// 按「中点与 a 是否同号」收缩，而不是硬编码「a 在下、b 在上」：
// AOS 是下→上穿、LOS 是上→下穿，写死方向的二分在 LOS 上第一次迭代就把两端
// 收到同一侧，于是收敛到一个错点（报出的 LOS 晚了 0.37°，是过境窗口用例抓出来的）。
double cross_el(Track& tr, double lo, double hi, double floor_deg) {
  double a = lo, b = hi;
  const double fa = tr.el(a) - floor_deg;
  for (int i = 0; i < 40 && b - a > 1e-6; ++i) {
    const double m = 0.5 * (a + b);
    const bool same_side_as_a = (tr.el(m) - floor_deg < 0.0) == (fa < 0.0);
    if (same_side_as_a) {
      a = m;
    } else {
      b = m;
    }
  }
  return 0.5 * (a + b);
}

// 极大仰角时刻：单峰，三分搜索比黄金分割直白，区间已经很小。
double peak_el(Track& tr, double lo, double hi) {
  double a = lo, b = hi;
  for (int i = 0; i < 60 && b - a > 1e-4; ++i) {
    const double m1 = a + (b - a) / 3.0;
    const double m2 = b - (b - a) / 3.0;
    if (tr.el(m1) < tr.el(m2))
      a = m1;
    else
      b = m2;
  }
  return 0.5 * (a + b);
}

}  // namespace

Look look_at(const libsgp4::Tle& tle, double tsince_min, const Station& st) {
  Track tr(tle, st);
  return tr.at(tsince_min);
}

double doppler_hz(const double rf_hz, const double range_rate_kms) {
  const double v_ms = range_rate_kms * 1000.0;
  return -v_ms / kC * rf_hz;
}

double linear_uplink_hz(const double f_up_hz, const double range_rate_kms) {
  const double v_ms = range_rate_kms * 1000.0;
  const double factor = 1.0 - v_ms / kC;  // 单程：收到的 = 发出的 × factor
  // 上行被移一次、下行再被移一次，同一段 ṙ/c 吃两遍，所以是平方。
  // 转发比（f_down/f_up）不参与：它只决定标称频率对，不决定多普勒。
  return f_up_hz / (factor * factor);
}

std::vector<Pass> find_passes(const libsgp4::Tle& tle, const Station& st,
                              double from_min, double to_min,
                              double el_floor_deg) {
  std::vector<Pass> out;
  if (to_min <= from_min) return out;
  Track tr(tle, st);

  const double coarse = 0.25;  // 分钟；LEO 过境几分钟到十几分钟，够密
  bool inside = false;
  Pass cur{};
  double prev = from_min - coarse;

  auto close = [&](double los) {
    cur.los_min = los;
    cur.tca_min = peak_el(tr, cur.aos_min, cur.los_min);
    const Look tca = tr.at(cur.tca_min);
    cur.max_el_deg = tca.el_deg;
    cur.min_range_km = tca.range_km;
    const double rr_a = tr.at(cur.aos_min).range_rate_kms;
    const double rr_l = tr.at(cur.los_min).range_rate_kms;
    cur.max_abs_range_rate_kms =
        std::max(std::max(std::abs(rr_a), std::abs(rr_l)),
                 cur.max_abs_range_rate_kms);
    out.push_back(cur);
    inside = false;
  };

  for (double t = from_min; t <= to_min + coarse; t += coarse) {
    const double el = tr.el(t);
    if (el >= el_floor_deg) {
      if (!inside) {
        inside = true;
        cur = Pass{};
        cur.aos_min = cross_el(tr, prev, t, el_floor_deg);
        cur.max_abs_range_rate_kms = 0.0;
      }
      cur.max_abs_range_rate_kms =
          std::max(cur.max_abs_range_rate_kms,
                   std::abs(tr.at(t).range_rate_kms));
    } else if (inside) {
      close(cross_el(tr, prev, t, el_floor_deg));
    }
    prev = t;
  }
  if (inside) close(to_min);
  return out;
}

}  // namespace radio::satellite
