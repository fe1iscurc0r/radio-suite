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
  cw_timer_.setSingleShot(true);
  connect(&cw_timer_, &QTimer::timeout, this, &RadioSession::endScriptedTx);
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
  transport_->send(core::civ::readRitFrame(false, radio_addr_));
  transport_->send(core::civ::readRitFrame(true, radio_addr_));
  transport_->send(core::civ::readOffsetFrame(radio_addr_));
  // 双 VFO（SUB 带）+ CW Keyer：0x25/0x26 选中/未选中两个 VFO、0x14 电平族、0x16 0x47
  transport_->send(core::civ::readVfoFreqFrame(false, radio_addr_));
  transport_->send(core::civ::readVfoFreqFrame(true, radio_addr_));
  transport_->send(core::civ::readVfoModeFrame(false, radio_addr_));
  transport_->send(core::civ::readVfoModeFrame(true, radio_addr_));
  transport_->send(core::civ::level14ReadFrame(core::civ::kSub14CwPitch, radio_addr_));
  transport_->send(core::civ::level14ReadFrame(core::civ::kSub14KeySpeed, radio_addr_));
  transport_->send(core::civ::level14ReadFrame(core::civ::kSub14BreakInDelay, radio_addr_));
  transport_->send(core::civ::readBreakInFrame(radio_addr_));
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
  } else if (auto vf = core::civ::parseVfoFreqResponse(*f)) {
    // 线上 sub=0 是「当前选中 VFO」，按 selected_sub 折算回 A/MAIN 与 B/SUB
    const bool to_sub = s.selected_sub ? !vf->unselected : vf->unselected;
    (to_sub ? s.sub_freq_hz : s.freq_hz) = vf->hz;
    changed = true;
  } else if (auto vm = core::civ::parseVfoModeResponse(*f)) {
    const bool to_sub = s.selected_sub ? !vm->unselected : vm->unselected;
    if (to_sub) {
      s.sub_mode = vm->mode;
      s.sub_filter = vm->filter;
    } else {
      s.mode = vm->mode;
      s.filter = vm->filter;
    }
    changed = true;
  } else if (auto mem = core::civ::parseMemoryChannelResponse(*f)) {
    s.memory_channel = *mem;
    s.memory_index = mem->channel;
    changed = true;
  } else if (auto menu = core::civ::parseMenuResponse(*f)) {
    const uint16_t key = static_cast<uint16_t>((static_cast<uint16_t>(menu->group) << 8) |
                                               menu->item);
    s.menu_values[key] = menu->value;
    changed = true;
  } else if (auto lv = core::civ::parseLevel14Response(*f, core::civ::kSub14CwPitch)) {
    s.cw_pitch_level = *lv;
    changed = true;
  } else if (auto lv = core::civ::parseLevel14Response(*f, core::civ::kSub14KeySpeed)) {
    s.cw_key_speed_level = *lv;
    changed = true;
  } else if (auto lv = core::civ::parseLevel14Response(*f, core::civ::kSub14BreakInDelay)) {
    s.break_in_delay_level = *lv;
    changed = true;
  } else if (auto bi = core::civ::parseBreakInResponse(*f)) {
    s.break_in = *bi;
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
  } else if (auto rit = core::civ::parseRitResponse(*f, false)) {
    s.rit = *rit;
    changed = true;
  } else if (auto dtx = core::civ::parseRitResponse(*f, true)) {
    s.dtx = *dtx;
    changed = true;
  } else if (auto off = core::civ::parseOffsetResponse(*f)) {
    s.offset_hz = *off;
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

bool RadioSession::beginScriptedTx() {
  const uint64_t now = nowMs();
  if (gate_.phase() != core::PttPhase::Tx) {
    if (!gate_.requestTx(now) || !gate_.confirmTx(now)) return false;
    const uint64_t hz = store_.snapshot().freq_hz;
    if (!core::SafetyGate::authorizeTx(hz)) {
      gate_.requestRx(now);
      last_phase_ = gate_.phase();
      emit pttPhaseChanged(static_cast<int>(last_phase_));
      emit txBlocked(static_cast<quint64>(hz));
      return false;
    }
    sendPtt(true);
  }
  last_phase_ = gate_.phase();
  emit pttPhaseChanged(static_cast<int>(last_phase_));
  return true;
}

void RadioSession::endScriptedTx() {
  if (gate_.phase() == core::PttPhase::Tx) {
    gate_.requestRx(nowMs());
    sendPtt(false);
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

void RadioSession::setSplit(bool on) {
  transport_->send(core::civ::splitFrame(on, radio_addr_));
  transport_->poll();
}

void RadioSession::setRit(bool on, bool dtx) {
  transport_->send(core::civ::ritFrame(on, dtx, radio_addr_));
  transport_->poll();
}

// ---------------- 双 VFO / SUB 带 ----------------

bool RadioSession::selectVfo(bool sub) {
  if (gate_.phase() == core::PttPhase::Tx) {
    qWarning() << "selectVfo refused while TX";
    return false;
  }
  transport_->send(core::civ::selectVfoFrame(sub, radio_addr_));
  transport_->poll();
  {
    core::RadioState s = store_.snapshot();
    s.selected_sub = sub;  // 0x07 无回读：本地记账
    s.last_update_ms = nowMs();
    store_.update(s);
    emit stateChanged();
  }
  pollRadio();  // 重新按 A/MAIN、B/SUB 折算回读
  return true;
}

bool RadioSession::swapVfo() {
  if (gate_.phase() == core::PttPhase::Tx) {
    qWarning() << "swapVfo refused while TX";
    return false;
  }
  transport_->send(core::civ::swapVfoFrame(radio_addr_));
  transport_->poll();
  pollRadio();
  return true;
}

bool RadioSession::equalizeVfo() {
  if (gate_.phase() == core::PttPhase::Tx) {
    qWarning() << "equalizeVfo refused while TX";
    return false;
  }
  transport_->send(core::civ::equalizeVfoFrame(radio_addr_));
  transport_->poll();
  pollRadio();
  return true;
}

bool RadioSession::tuneSubTo(uint64_t hz) {
  if (!core::SafetyGate::authorizeSetFreq(hz)) {
    qWarning() << "tuneSubTo rejected by RX range:" << hz << "Hz";
    emit tuneRejected(static_cast<quint64>(hz));
    return false;
  }
  // Split 时 TX 频率 = 另一 VFO：发射中改它等同改 TX 频率
  if (gate_.phase() == core::PttPhase::Tx && store_.snapshot().split &&
      !core::SafetyGate::authorizeTx(hz)) {
    qWarning() << "tuneSubTo rejected while TX (non-amateur target):" << hz << "Hz";
    emit txBlocked(static_cast<quint64>(hz));
    return false;
  }
  transport_->send(core::civ::setVfoFreqFrame(true, hz, radio_addr_));
  transport_->poll();
  pollRadio();
  return true;
}

bool RadioSession::setSubMode(core::civ::Mode m, int data_mode, int filter) {
  transport_->send(
      core::civ::setVfoModeFrame(true, m, data_mode, filter, radio_addr_));
  transport_->poll();
  pollRadio();
  return true;
}

// ---------------- CW Keyer ----------------

bool RadioSession::sendCwText(std::string_view text, int wpm) {
  auto frames = core::civ::cwSendFrames(text, radio_addr_);
  if (frames.empty()) return false;
  if (wpm < 6) wpm = 6;
  if (wpm > 48) wpm = 48;
  if (!beginScriptedTx()) return false;  // 非业余段/门禁拒绝：不键入
  size_t chars = 0;
  for (const auto& fr : frames) chars += fr.size() > 6 ? fr.size() - 6 : 0;
  for (const auto& fr : frames) transport_->send(fr);
  transport_->poll();
  // PARIS 估时：1 字符 ≈ 1.2 s / WPM；+1 s 余量避免提前收尾
  const int ms = static_cast<int>(chars * 1200u / static_cast<size_t>(wpm)) + 1000;
  cw_timer_.start(ms);
  return true;
}

void RadioSession::stopCw() {
  cw_timer_.stop();
  transport_->send(core::civ::cwStopFrame(radio_addr_));
  transport_->poll();
  endScriptedTx();
}

void RadioSession::setKeySpeed(int wpm) {
  const int level = core::civ::wpmToKeySpeedLevel(wpm);
  transport_->send(core::civ::level14WriteFrame(core::civ::kSub14KeySpeed, level,
                                                radio_addr_));
  transport_->send(core::civ::level14ReadFrame(core::civ::kSub14KeySpeed, radio_addr_));
  transport_->poll();
}

void RadioSession::setCwPitch(int hz) {
  const int level = core::civ::hzToCwPitchLevel(hz);
  transport_->send(core::civ::level14WriteFrame(core::civ::kSub14CwPitch, level,
                                                radio_addr_));
  transport_->send(core::civ::level14ReadFrame(core::civ::kSub14CwPitch, radio_addr_));
  transport_->poll();
}

void RadioSession::setBreakIn(bool on) {
  transport_->send(core::civ::breakInFrame(on, radio_addr_));
  transport_->send(core::civ::readBreakInFrame(radio_addr_));
  transport_->poll();
}

void RadioSession::setBreakInDelay(int raw) {
  transport_->send(core::civ::level14WriteFrame(core::civ::kSub14BreakInDelay, raw,
                                                radio_addr_));
  transport_->send(
      core::civ::level14ReadFrame(core::civ::kSub14BreakInDelay, radio_addr_));
  transport_->poll();
}

void RadioSession::readCwSettings() {
  transport_->send(core::civ::level14ReadFrame(core::civ::kSub14CwPitch, radio_addr_));
  transport_->send(core::civ::level14ReadFrame(core::civ::kSub14KeySpeed, radio_addr_));
  transport_->send(
      core::civ::level14ReadFrame(core::civ::kSub14BreakInDelay, radio_addr_));
  transport_->send(core::civ::readBreakInFrame(radio_addr_));
  transport_->poll();
}

// ---------------- Set Mode 菜单项 ----------------

void RadioSession::readMenuItem(uint8_t group, uint8_t item) {
  transport_->send(core::civ::menuReadFrame(group, item, radio_addr_));
  transport_->poll();
}

void RadioSession::writeMenuItem(uint8_t group, uint8_t item,
                                 std::span<const uint8_t> value) {
  transport_->send(core::civ::menuWriteFrame(group, item, value, radio_addr_));
  transport_->send(core::civ::menuReadFrame(group, item, radio_addr_));
  transport_->poll();
}

// ---------------- 记忆信道 ----------------

void RadioSession::readMemoryChannel(int channel) {
  if (channel < 0 || channel > 99) {
    qWarning() << "readMemoryChannel out of range:" << channel;
    return;
  }
  transport_->send(core::civ::readMemoryChannelFrame(channel, radio_addr_));
  transport_->poll();
}

void RadioSession::writeMemoryChannel(const core::civ::MemoryChannel& mem) {
  if (mem.channel < 0 || mem.channel > 99) {
    qWarning() << "writeMemoryChannel out of range:" << mem.channel;
    return;
  }
  transport_->send(core::civ::writeMemoryChannelFrame(mem, radio_addr_));
  transport_->send(core::civ::readMemoryChannelFrame(mem.channel, radio_addr_));
  transport_->poll();
}

void RadioSession::selectMemoryGroup(int group) {
  if (group < 0 || group > 99) {
    qWarning() << "selectMemoryGroup out of range:" << group;
    return;
  }
  transport_->send(core::civ::selectMemoryGroupFrame(group, radio_addr_));
  transport_->poll();
  core::RadioState s = store_.snapshot();
  s.memory_group = group;
  s.last_update_ms = nowMs();
  store_.update(s);
  emit stateChanged();
}

void RadioSession::selectMemoryChannel(int channel) {
  if (channel < 0 || channel > 99) {
    qWarning() << "selectMemoryChannel out of range:" << channel;
    return;
  }
  transport_->send(core::civ::selectMemoryChannelFrame(channel, radio_addr_));
  transport_->poll();
  core::RadioState s = store_.snapshot();
  s.memory_index = channel;
  s.last_update_ms = nowMs();
  store_.update(s);
  emit stateChanged();
}

void RadioSession::sendPtt(bool on) {
  if (last_ptt_sent_ == on) return;
  transport_->send(core::civ::pttFrame(on, radio_addr_));
  transport_->poll();
  last_ptt_sent_ = on;
}

}  // namespace radio::app
