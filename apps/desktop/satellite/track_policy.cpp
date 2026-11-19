// SPDX-License-Identifier: AGPL-3.0-or-later
// radio-suite · satellite/track_policy.cpp
#include "track_policy.h"

#include <cmath>

namespace radio::satellite {

TrackDecision trackDecide(const TrackConfig& cfg, double target_hz,
                          double current_hz, bool tx_active, int64_t now_ms,
                          int64_t last_write_ms) {
  // 发射中一律不写（既有约束）
  if (tx_active) return {false, current_hz, "tx active"};
  // 最小间隔未到 → 不写
  if (last_write_ms > 0 && now_ms - last_write_ms < cfg.min_interval_ms)
    return {false, current_hz, "interval"};
  // 死区内不写（省 CI-V 通道）
  if (std::abs(target_hz - current_hz) < cfg.deadzone_hz)
    return {false, current_hz, "deadzone"};
  return {true, target_hz, "write"};
}

}  // namespace radio::satellite
