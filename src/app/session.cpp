// SPDX-License-Identifier: AGPL-3.0-or-later
#include "app/session.h"

#include <QDateTime>
#include <QDebug>

namespace radio::app {

RadioSession::RadioSession(std::shared_ptr<core::ICivTransport> transport,
                           uint8_t radio_addr, QObject* parent)
    : QObject(parent), transport_(std::move(transport)), radio_addr_(radio_addr) {
  transport_->setFrameCallback(
      [this](std::span<const uint8_t> f) { onFrame(f); });
  connect(&poll_timer_, &QTimer::timeout, this, &RadioSession::pollRadio);
  connect(&tick_timer_, &QTimer::timeout, this, &RadioSession::onTick);
}

void RadioSession::start() {
  if (!transport_->isOpen()) {
    std::string err;
    if (!transport_->open(&err)) {
      qWarning() << "transport open failed:" << QString::fromStdString(err);
      return;
    }
  }
  poll_timer_.start(250);
  tick_timer_.start(100);
  pollRadio();
}

void RadioSession::stop() {
  poll_timer_.stop();
  tick_timer_.stop();
  if (gate_.phase() == core::PttPhase::Tx) {
    gate_.requestRx(nowMs());
    sendPtt(false);
  }
  transport_->close();
}

uint64_t RadioSession::nowMs() const {
  return static_cast<uint64_t>(QDateTime::currentMSecsSinceEpoch());
}

void RadioSession::pollRadio() {
  transport_->send(core::civ::readFreqFrame(radio_addr_));
  transport_->send(core::civ::readModeFrame(radio_addr_));
  transport_->send(core::civ::readSmeterFrame(radio_addr_));
  transport_->send(core::civ::readMeterFrame(radio_addr_));
  // 功能开关回读（0x11 + 0x16 族 + 0x0F），mock/真机均支持裸查询
  transport_->send(core::civ::readAttFrame(radio_addr_));
  transport_->send(core::civ::sw16ReadFrame(core::civ::kSub16Nb, radio_addr_));
  transport_->send(core::civ::sw16ReadFrame(core::civ::kSub16Nr, radio_addr_));
  transport_->send(core::civ::sw16ReadFrame(core::civ::kSub16Moni, radio_addr_));
  transport_->send(core::civ::sw16ReadFrame(core::civ::kSub16NotchAuto, radio_addr_));
  transport_->send(core::civ::sw16ReadFrame(core::civ::kSub16Vox, radio_addr_));
  transport_->send(core::civ::sw16ReadFrame(core::civ::kSub16Pamp, radio_addr_));
  transport_->send(core::civ::sw16ReadFrame(core::civ::kSub16Agc, radio_addr_));
  transport_->send(core::civ::readDuplexFrame(radio_addr_));
  transport_->poll();  // mock：同步回调 onFrame
}

void RadioSession::onTick() {
  const uint64_t now = nowMs();
  const core::PttPhase before = gate_.phase();
  gate_.tick(now);
  const core::PttPhase after = gate_.phase();
  if (before != after) {
    // TX 超时自动回 RX：门禁驱动，命令补发在此同步
    if (before == core::PttPhase::Tx && after == core::PttPhase::Rx) sendPtt(false);
    if (after != last_phase_) {
      last_phase_ = after;
      emit pttPhaseChanged(static_cast<int>(after));
    }
  }
}

void RadioSession::onFrame(std::span<const uint8_t> raw) {
  auto f = core::civ::parseFrame(raw);
  if (!f) return;

  core::RadioState s = store_.snapshot();
  bool changed = false;
  if (auto hz = core::civ::parseFreqResponse(*f)) {
    s.freq_hz = *hz;
    changed = true;
  } else if (auto mi = core::civ::parseModeResponse(*f)) {
    s.mode = mi->mode;
    s.filter = mi->filter;
    changed = true;
  } else if (auto sm = core::civ::parseSmeterResponse(*f)) {
    s.smeter_raw = *sm;
    s.smeter_dbm = core::civ::smeterToDbm(*sm);
    changed = true;
  } else if (auto mi = core::civ::parseMeterResponse(*f)) {
    s.po_w = static_cast<float>(mi->po_raw) * 0.1f;
    s.swr = static_cast<float>(mi->swr_raw) * 0.1f;
    s.alc = mi->alc;
    changed = true;
  } else if (auto ptt = core::civ::parsePttResponse(*f)) {
    s.ptt_on = *ptt;
    changed = true;
  } else if (auto att = core::civ::parseAttResponse(*f)) {
    s.att_20db = *att == 0x20;
    changed = true;
  } else if (auto nb = core::civ::parseSw16Response(*f, core::civ::kSub16Nb)) {
    s.nb = *nb != 0;
    changed = true;
  } else if (auto nr = core::civ::parseSw16Response(*f, core::civ::kSub16Nr)) {
    s.nr = *nr != 0;
    changed = true;
  } else if (auto moni = core::civ::parseSw16Response(*f, core::civ::kSub16Moni)) {
    s.moni = *moni != 0;
    changed = true;
  } else if (auto na = core::civ::parseSw16Response(*f, core::civ::kSub16NotchAuto)) {
    s.notch_auto = *na != 0;
    changed = true;
  } else if (auto vox = core::civ::parseSw16Response(*f, core::civ::kSub16Vox)) {
    s.vox = *vox != 0;
    changed = true;
  } else if (auto pamp = core::civ::parseSw16Response(*f, core::civ::kSub16Pamp)) {
    s.pamp = *pamp;
    changed = true;
  } else if (auto agc = core::civ::parseSw16Response(*f, core::civ::kSub16Agc)) {
    s.agc = *agc;
    changed = true;
  } else if (auto dup = core::civ::parseDuplexResponse(*f)) {
    s.duplex_raw = *dup;
    changed = true;
  } else if (core::civ::isNak(*f)) {
    qWarning() << "radio NAK — 命令被拒（频段限制或参数）";  // M3：进原始帧抽屉
  }
  if (changed) {
    s.last_update_ms = nowMs();
    store_.update(s);
    emit stateChanged();
  }
}

void RadioSession::pttButtonPressed() {
  const uint64_t now = nowMs();
  switch (gate_.phase()) {
    case core::PttPhase::Rx:
      gate_.requestTx(now);
      break;
    case core::PttPhase::ConfirmPending:
      if (gate_.confirmTx(now)) {
        // 发射门禁最后一道：非业余段（广播/航空等）可听不可发
        const uint64_t hz = store_.snapshot().freq_hz;
        if (!core::SafetyGate::authorizeTx(hz)) {
          gate_.requestRx(now);
          emit txBlocked(static_cast<quint64>(hz));
        } else {
          sendPtt(true);
        }
      }
      break;
    case core::PttPhase::Tx:
      gate_.requestRx(now);
      sendPtt(false);
      break;
  }
  last_phase_ = gate_.phase();
  emit pttPhaseChanged(static_cast<int>(last_phase_));
}

bool RadioSession::tuneTo(uint64_t hz) {
  if (!core::SafetyGate::authorizeSetFreq(hz)) {
    qWarning() << "tuneTo rejected by RX range:" << hz << "Hz";
    emit tuneRejected(static_cast<quint64>(hz));
    return false;
  }
  // 发射中调谐：目标频率必须仍可发射（否则把 TX 频率挪出业余段=违规发射）
  if (gate_.phase() == core::PttPhase::Tx &&
      !core::SafetyGate::authorizeTx(hz)) {
    qWarning() << "tuneTo rejected while TX (non-amateur target):" << hz << "Hz";
    emit txBlocked(static_cast<quint64>(hz));
    return false;
  }
  transport_->send(core::civ::setFreqFrame(hz, radio_addr_));
  transport_->poll();
  return true;
}

bool RadioSession::tuneBy(int64_t delta_hz) {
  const uint64_t cur = store_.snapshot().freq_hz;
  if (delta_hz < 0 && cur < static_cast<uint64_t>(-delta_hz)) return false;
  const uint64_t target = cur + static_cast<uint64_t>(delta_hz);
  return tuneTo(target);
}

void RadioSession::setMode(core::civ::Mode m) {
  transport_->send(core::civ::setModeFrame(m, 0, radio_addr_));
  transport_->poll();
}

void RadioSession::setAtt(bool on_20db) {
  transport_->send(core::civ::attFrame(on_20db, radio_addr_));
  transport_->poll();
}

void RadioSession::setSw16(uint8_t sub, uint8_t value) {
  transport_->send(core::civ::sw16SetFrame(sub, value, radio_addr_));
  transport_->poll();
}

void RadioSession::setDuplex(uint8_t val) {
  transport_->send(core::civ::duplexFrame(val, radio_addr_));
  transport_->poll();
}

void RadioSession::sendPtt(bool on) {
  if (last_ptt_sent_ == on) return;
  transport_->send(core::civ::pttFrame(on, radio_addr_));
  transport_->poll();
  last_ptt_sent_ = on;
}

}  // namespace radio::app
