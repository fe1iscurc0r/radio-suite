// SPDX-License-Identifier: AGPL-3.0-or-later
// radio-suite · core/safety.h — PTT 二次确认 + 设频白名单门禁（全仓唯一出口）
//
// 工单硬约束 2：安全门禁在 C++ 侧，不可被 UI 绕过；UI 确认框只作交互呈现。
// 计时归属：deadline 判定全在本类（core）；UI 的 QTimer 只负责喂数字时钟。
#pragma once

#include <cstdint>

#include "core/bands.h"

namespace radio::core {

enum class PttPhase {
  Rx,              // 接收（常态）
  ConfirmPending,  // 已收到第一次请求，等待窗口内二次确认
  Tx,              // 发射中（有自动回 RX 死限）
};

class SafetyGate {
 public:
  static constexpr uint64_t kConfirmWindowMs = 5'000;         // 二次确认窗口
  static constexpr uint64_t kTxTimeoutMs = 10 * 60 * 1'000;   // 10 分钟自动回 RX

  // 第一次请求发射。Rx→ConfirmPending；已在 Tx 或窗口未过期时按状态机语义处理。
  // 返回是否处于「等待确认」。
  bool requestTx(uint64_t now_ms);
  // 二次确认。窗口内 -> Tx（写 tx 死限）；窗口已过 -> 回 Rx。返回是否进入 Tx。
  bool confirmTx(uint64_t now_ms);
  // 主动回接收（任意状态）。
  void requestRx(uint64_t now_ms);
  // 推进时钟：确认窗口过期回 Rx；TX 超时自动回 Rx。由持有者周期调用。
  void tick(uint64_t now_ms);

  PttPhase phase() const { return phase_; }
  uint64_t txDeadlineMs() const { return tx_deadline_ms_; }     // 0 = 不在 Tx
  uint64_t confirmDeadlineMs() const { return confirm_deadline_ms_; }

  // 设频门禁（唯一出口）：接收调谐按宽 RX 范围；发射频率按业余白名单。
  static bool authorizeSetFreq(uint64_t hz) { return isRxAllowed(hz); }
  // 发射门禁：进入 TX 前最后检查——不在业余段一律拒绝（广播/航空可听不可发）。
  static bool authorizeTx(uint64_t hz) { return isAllowedFreq(hz); }

 private:
  PttPhase phase_ = PttPhase::Rx;
  uint64_t confirm_deadline_ms_ = 0;
  uint64_t tx_deadline_ms_ = 0;
};

}  // namespace radio::core
