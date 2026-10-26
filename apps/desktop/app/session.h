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
  const core::SafetyGate& gate() const { return gate_; }

  // ---- UI/总线命令（唯一出口，禁止绕过）----
  void pttButtonPressed();          // PTT 键按下：Rx→待确认→确认进入 Tx→再按回 Rx
  bool beginScriptedTx();           // 一键发射：一次确认即进入 TX，并实际发送 PTT ON
  void endScriptedTx();             // 播放结束/失败均回 RX；重复调用不重复发帧
  bool tuneTo(uint64_t hz);         // 设频（接收按宽 RX 范围门禁）
  bool tuneBy(int64_t delta_hz);    // 步进
  void setMode(core::civ::Mode m);
  void setAtt(bool on_20db);        // 0x11（电台侧仅 HF/50MHz 生效，NAK 由状态栏呈现）
  void setSw16(uint8_t sub, uint8_t value);  // 0x16 族（NB/NR/MONI/PAMP/AGC/...）
  void setDuplex(uint8_t val);      // 0x0F：0x10 simplex/0x11 dup-/0x12 dup+（⚠️ M1 需叠加频偏后的 TX 段校验）
  void setSplit(bool on);           // 0x0F 0x00/0x01（⚠️ 发射频=另一 VFO，合规自查）
  void setRit(bool on, bool dtx);   // 0x21 0x01/0x02

 signals:
  void stateChanged();  // RadioState 有更新
  void pttPhaseChanged(int phase);
  void tuneRejected(quint64 hz);  // RX 范围外拒绝设频（UI 提示用）
  void txBlocked(quint64 hz);     // 发射门禁：非业余段拒绝进入 TX

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
