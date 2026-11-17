// SPDX-License-Identifier: AGPL-3.0-or-later
// radio-suite · satellite/sat_track.h — 卫星过境与多普勒（纯标准库 + vendored libsgp4）
//
// 这一层只回答三件事：此刻卫星在我站的哪个方向、距离变化率多少、于是上下行该打什么频率。
// 不碰 Qt、不碰会话：会话层的 SUB 双 VFO 与 effectiveTxHz 才是执行者，这里只出数。
//
// 单位约定（libsgp4 的坑就在这儿，全部在这一层换算成对外一致的量）：
//   Observer 入参：纬度/经度=度，海拔=公里
//   GetLookAngle 出参：方位/仰角=**弧度**，斜距=公里，距离变化率=公里/秒
//   tsince：相对 TLE 历元的分钟数（与 libsgp4 一致，不另造时间轴）
#pragma once

#include <libsgp4/Tle.h>

#include <vector>

namespace radio::satellite {

struct Station {
  double lat_deg{};
  double lon_deg{};
  double alt_km{};
};

struct Look {
  double az_deg{};           // 0..360
  double el_deg{};           // -90..90
  double range_km{};
  double range_rate_kms{};   // 正 = 距离在增大（远离）
};

// 传播 + 观测。libsgp4 在异常轨道上会抛 SatelliteException，这里原样透出，
// 不吞——吞掉的话「算不出」就变成「算出来是 0」，那是更难查的错。
Look look_at(const libsgp4::Tle& tle, double tsince_min, const Station& st);

// ---- W2.1 时基 ----
// tsince = (UTC_now − TLE 历元) 的分钟数。跨日/跨年/闰秒无所谓：SGP4 用 UT1≈UTC，
// 本仓不引 EOP 自算（总纲 §5.2）。TLE 第 1 行历元只有 0.1 分钟量化（±3 s），
// 是数据特性不是 bug，不修。历元之前 tsince 为负，属正常值（SGP4 支持反向传播）。
double tsince_min(const libsgp4::Tle& tle, const libsgp4::DateTime& now_utc);

// 单程多普勒：ṙ>0（远离）⇒ 收到的频率降低。返回的是「相对标称的偏移量」，
// 不是绝对频率，调用方自己加。
double doppler_hz(double rf_hz, double range_rate_kms);

// 线性转发器的上行预补偿：地面该发多少赫兹，才能让卫星转发后我站收到标称下行。
// 上行被移一次、下行再被移一次，同一段 ṙ/c 各吃一遍，所以是 (1 - ṙ/c)^-2 倍
// —— 业余圈说的「线性星上行要补两倍多普勒」就是它。转发比不进来：
// 它只决定标称频率对，不决定多普勒量。
double linear_uplink_hz(double f_up_hz, double range_rate_kms);

struct Pass {
  double aos_min{};         // 相对 TLE 历元的分钟
  double tca_min{};
  double los_min{};
  double max_el_deg{};
  double min_range_km{};
  double max_abs_range_rate_kms{};  // 整个过境里 |ṙ| 的最大值（出现在 AOS/LOS 附近）
};

// 在 [from_min, to_min] 内找仰角 ≥ el_floor_deg 的窗口。
// 粗采样定界，再对仰角过零点做二分收敛；TCA 用三分搜索细化。
std::vector<Pass> find_passes(const libsgp4::Tle& tle, const Station& st,
                              double from_min, double to_min,
                              double el_floor_deg);

}  // namespace radio::satellite
