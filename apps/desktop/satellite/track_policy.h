// SPDX-License-Identifier: AGPL-3.0-or-later
// radio-suite · satellite/track_policy.h — W2.4 双 VFO 自动跟踪的写频策略（纯函数）
//
// 总纲 §5.5：这一层只算「现在该不该写频、写多少」，不直发命令；执行者在会话层。
// 三条硬约束（A-2.6 的工具层基础）：
//   · |Δf| < 死区 → 不写；
//   · 两次写之间 ≥ 最小间隔；
//   · 发射中一律不写（既有约束：发射中拒绝切换 VFO）。
#pragma once

#include <cstdint>
#include <string>

namespace radio::satellite {

struct TrackConfig {
  double deadzone_hz = 200.0;
  int64_t min_interval_ms = 1000;
  double preaos_s = 30.0;  // AOS 前多少秒进入预置窗
};

struct TrackDecision {
  bool write = false;
  double rx_hz = 0.0;
  std::string reason;
};

// target_hz 恒定（AOS 前预置 → 过境中每秒重算多普勒 → LOS 后显式停）：
// 现刻多普勒由调用方把 look_at 的 ṙ 喂给 doppler_hz 算出 target；
// 发射中（tx_active）一律不写。
TrackDecision trackDecide(const TrackConfig& cfg, double target_hz,
                          double current_hz, bool tx_active, int64_t now_ms,
                          int64_t last_write_ms);

}  // namespace radio::satellite
