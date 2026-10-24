// SPDX-License-Identifier: AGPL-3.0-or-later
// radio-suite · app/session.h — RadioSession：transport 会话 + 安全门禁 + 状态单源
//
// 工单硬约束 2/4：UI 与总线共用同一套命令出口（本类）；PTT/设频必经 SafetyGate。
// M0 单线程（QTimer 驱动）；M1 把 poll 移入 core I/O 线程，跨线程只传值。
#pragma once

#include <QTimer>
#include <cstdint>
#include <memory>

#include "core/civ.h"
#include "core/safety.h"
#include "core/state.h"
#include "core/transport.h"

namespace radio::app {

class RadioSession : public QObject {
  Q_OBJECT
 public:
  explicit RadioSession(std::shared_ptr<core::ICivTransport> transport,
                        uint8_t radio_addr = core::civ::kIc705Addr,
                        QObject* parent = nullptr);

  void start();  // 打开 transport，启动轮询与门禁时钟
  void stop();

  core::RadioStateStore& store() { return store_; }
  core::SafetyGate& gate() { return gate_; }

  // ---- UI/总线命令（唯一出口，禁止绕过）----
  void pttButtonPressed();          // PTT 键按下：Rx→待确认→确认进入 Tx→再按回 Rx
  bool tuneTo(uint64_t hz);         // 设频（白名单外拒绝）
  bool tuneBy(int64_t delta_hz);    // 步进
  void setMode(core::civ::Mode m);

 signals:
  void stateChanged();  // RadioState 有更新
  void pttPhaseChanged(int phase);
  void tuneRejected(quint64 hz);  // 白名单/电台拒绝设频（UI 提示用）

 private:
  void onFrame(std::span<const uint8_t> raw);
  void pollRadio();
  void onTick();
  void sendPtt(bool on);
  uint64_t nowMs() const;

  std::shared_ptr<core::ICivTransport> transport_;
  uint8_t radio_addr_;
  core::RadioStateStore store_;
  core::SafetyGate gate_;
  QTimer poll_timer_;
  QTimer tick_timer_;
  bool last_ptt_sent_ = false;
  core::PttPhase last_phase_ = core::PttPhase::Rx;
};

}  // namespace radio::app
