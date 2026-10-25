// SPDX-License-Identifier: AGPL-3.0-or-later
#include "core/safety.h"

namespace radio::core {

bool SafetyGate::requestTx(uint64_t now_ms) {
  if (phase_ == PttPhase::Tx) return false;  // 已在发射，不重置死限
  phase_ = PttPhase::ConfirmPending;
  confirm_deadline_ms_ = now_ms + kConfirmWindowMs;
  return true;
}

bool SafetyGate::confirmTx(uint64_t now_ms) {
  if (phase_ != PttPhase::ConfirmPending) return false;
  if (now_ms > confirm_deadline_ms_) {      // 窗口过期：作废，回 Rx
    phase_ = PttPhase::Rx;
    confirm_deadline_ms_ = 0;
    return false;
  }
  phase_ = PttPhase::Tx;
  confirm_deadline_ms_ = 0;
  tx_deadline_ms_ = now_ms + kTxTimeoutMs;
  return true;
}

void SafetyGate::requestRx(uint64_t /*now_ms*/) {
  phase_ = PttPhase::Rx;
  confirm_deadline_ms_ = 0;
  tx_deadline_ms_ = 0;
}

void SafetyGate::tick(uint64_t now_ms) {
  if (phase_ == PttPhase::ConfirmPending && now_ms > confirm_deadline_ms_) {
    phase_ = PttPhase::Rx;
    confirm_deadline_ms_ = 0;
  }
  if (phase_ == PttPhase::Tx && now_ms > tx_deadline_ms_) {
    phase_ = PttPhase::Rx;
    tx_deadline_ms_ = 0;
  }
}

}  // namespace radio::core
