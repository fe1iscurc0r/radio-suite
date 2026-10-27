// SPDX-License-Identifier: AGPL-3.0-or-later
#include "app/repeater_dialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFont>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>

#include <cmath>

#include "app/session.h"

namespace radio::app {
namespace {

QString fmtFreq(uint64_t hz) {
  return QString("%1 MHz").arg(hz / 1e6, 0, 'f', 6);
}

QString fmtOffset(uint32_t hz) {
  return QString("%1 kHz").arg(hz / 1000.0, 0, 'f', 1);
}

QString fmtTone(uint16_t decihz) {
  return QString("%1 Hz").arg(decihz / 10.0, 0, 'f', 1);
}

QString fmtDtcs(int code) {
  return QString("%1").arg(code, 3, 10, QChar('0'));
}

void selectData(QComboBox* box, int value) {
  const int idx = box->findData(value);
  if (idx >= 0) box->setCurrentIndex(idx);
}

void addToneItems(QComboBox* box) {
  for (uint16_t decihz : radio::core::civ::ctcssTonesDecihz())
    box->addItem(fmtTone(decihz), static_cast<int>(decihz));
}

}  // namespace

RepeaterDialog::RepeaterDialog(RadioSession* session, QWidget* parent)
    : QDialog(parent), session_(session) {
  setWindowTitle("Repeater / Tone — 频差与亚音");
  setMinimumWidth(560);

  auto* root = new QVBoxLayout(this);

  // 收发双频：DUP± 时 TX 由 RX 与频差算出，不猜惯例值
  auto* pair = new QGroupBox("收发频率", this);
  auto* pair_lay = new QVBoxLayout(pair);
  QFont mono("Cascadia Mono");
  mono.setStyleHint(QFont::Monospace);
  mono.setPointSize(14);
  mono.setBold(true);
  rx_label_ = new QLabel("RX  —", pair);
  rx_label_->setObjectName("rep_rx");
  rx_label_->setFont(mono);
  rx_label_->setStyleSheet("color: #7ee0a0;");
  tx_label_ = new QLabel("TX  —", pair);
  tx_label_->setObjectName("rep_tx");
  tx_label_->setFont(mono);
  tx_label_->setStyleSheet("color: #ff8a65;");
  pair_lay->addWidget(rx_label_);
  pair_lay->addWidget(tx_label_);
  root->addWidget(pair);

  // 频差（0x0F 方向 + 0x0D 偏移，100Hz 分辨率）
  auto* duplex = new QGroupBox("中继频差", this);
  auto* duplex_form = new QFormLayout(duplex);
  dir_ = new QComboBox(duplex);
  dir_->setObjectName("rep_dir");
  dir_->addItem("同频 SPLX", 0x10);
  dir_->addItem("下差 DUP−", 0x11);
  dir_->addItem("上差 DUP+", 0x12);
  duplex_form->addRow("方向 (0x0F)", dir_);

  offset_mhz_ = new QDoubleSpinBox(duplex);
  offset_mhz_->setObjectName("rep_offset");
  offset_mhz_->setRange(0.0, 9.9999);
  offset_mhz_->setDecimals(4);
  offset_mhz_->setSingleStep(0.0001);  // 100 Hz
  offset_mhz_->setSuffix(" MHz");
  duplex_form->addRow("频差 (0x0D)", offset_mhz_);

  auto* duplex_apply = new QPushButton("应用频差", duplex);
  duplex_apply->setObjectName("rep_apply");
  duplex_form->addRow(duplex_apply);
  root->addWidget(duplex);

  // 模拟亚音：0x16 0x5D 模式 + 0x1B 0x00/0x01 频率
  auto* tone = new QGroupBox("模拟亚音（CTCSS / TSQL）", this);
  auto* tone_form = new QFormLayout(tone);

  tone_mode_ = new QComboBox(tone);
  tone_mode_->setObjectName("rep_tone_mode");
  const std::pair<const char*, radio::core::civ::ToneSquelch> kModes[] = {
      {"OFF", radio::core::civ::ToneSquelch::Off},
      {"TONE（模拟亚音发射）", radio::core::civ::ToneSquelch::Tone},
      {"TSQL（模拟亚音静噪）", radio::core::civ::ToneSquelch::Tsql},
      {"DTCS（数字亚音）", radio::core::civ::ToneSquelch::Dtcs},
      {"DTCS(T)", radio::core::civ::ToneSquelch::DtcsT},
      {"TONE(T)/DTCS(R)", radio::core::civ::ToneSquelch::ToneTDtcsR},
      {"DTCS(T)/TSQL(R)", radio::core::civ::ToneSquelch::DtcsTTsqlR},
      {"TONE(T)/TSQL(R)", radio::core::civ::ToneSquelch::ToneTTsqlR},
  };
  for (const auto& [name, mode] : kModes)
    tone_mode_->addItem(name, static_cast<int>(mode));
  tone_form->addRow("模式 (0x16 0x5D)", tone_mode_);

  tone_freq_ = new QComboBox(tone);
  tone_freq_->setObjectName("rep_tone_freq");
  addToneItems(tone_freq_);
  tone_form->addRow("中继音 (0x1B 00)", tone_freq_);

  tsql_freq_ = new QComboBox(tone);
  tsql_freq_->setObjectName("rep_tsql_freq");
  addToneItems(tsql_freq_);
  tone_form->addRow("TSQL 频率 (0x1B 01)", tsql_freq_);

  auto* tone_buttons = new QHBoxLayout();
  auto* mode_apply = new QPushButton("应用模式", tone);
  mode_apply->setObjectName("rep_tone_apply");
  auto* tone_apply = new QPushButton("应用中继音", tone);
  tone_apply->setObjectName("rep_tone_freq_apply");
  auto* tsql_apply = new QPushButton("应用 TSQL", tone);
  tsql_apply->setObjectName("rep_tsql_apply");
  tone_buttons->addWidget(mode_apply);
  tone_buttons->addWidget(tone_apply);
  tone_buttons->addWidget(tsql_apply);
  tone_form->addRow(tone_buttons);

  sw_tone_ = new QPushButton("中继音开关", tone);
  sw_tone_->setObjectName("rep_sw_tone");
  sw_tone_->setCheckable(true);
  sw_tsql_ = new QPushButton("TSQL 开关", tone);
  sw_tsql_->setObjectName("rep_sw_tsql");
  sw_tsql_->setCheckable(true);
  sw_dtcs_ = new QPushButton("DTCS 开关", tone);
  sw_dtcs_->setObjectName("rep_sw_dtcs");
  sw_dtcs_->setCheckable(true);
  auto* switches = new QHBoxLayout();
  switches->addWidget(sw_tone_);
  switches->addWidget(sw_tsql_);
  switches->addWidget(sw_dtcs_);
  switches->addStretch(1);
  tone_form->addRow("独立开关 (0x42/43/4B)", switches);
  root->addWidget(tone);

  // 数字亚音：0x1B 0x02 DTCS 码 + 收发极性
  auto* dtcs = new QGroupBox("数字亚音（DTCS）", this);
  auto* dtcs_form = new QFormLayout(dtcs);
  dtcs_code_ = new QComboBox(dtcs);
  dtcs_code_->setObjectName("rep_dtcs_code");
  for (uint16_t code : radio::core::civ::dtcsCodes())
    dtcs_code_->addItem(fmtDtcs(code), static_cast<int>(code));
  dtcs_form->addRow("码 (0x1B 02)", dtcs_code_);

  dtcs_rx_pol_ = new QComboBox(dtcs);
  dtcs_rx_pol_->setObjectName("rep_dtcs_rx");
  dtcs_rx_pol_->addItem("Normal", 0);
  dtcs_rx_pol_->addItem("Reverse", 1);
  dtcs_form->addRow("接收极性", dtcs_rx_pol_);

  dtcs_tx_pol_ = new QComboBox(dtcs);
  dtcs_tx_pol_->setObjectName("rep_dtcs_tx");
  dtcs_tx_pol_->addItem("Normal", 0);
  dtcs_tx_pol_->addItem("Reverse", 1);
  dtcs_form->addRow("发射极性", dtcs_tx_pol_);

  auto* dtcs_apply = new QPushButton("应用 DTCS", dtcs);
  dtcs_apply->setObjectName("rep_dtcs_apply");
  dtcs_form->addRow(dtcs_apply);
  root->addWidget(dtcs);

  readback_ = new QLabel("—", this);
  readback_->setObjectName("rep_readback");
  readback_->setStyleSheet("color: #4dd0e1;");
  readback_->setWordWrap(true);
  root->addWidget(readback_);

  auto* note = new QLabel(
      "0x1B 频率为 0.1Hz 单位 3 字节 BCD（88.5Hz → 00 08 85）。DTCS 极性字节序按"
      "指南 p.21 推定（高半字节 TX / 低半字节 RX），真机待复验。",
      this);
  note->setStyleSheet("color: #9e9e9e;");
  note->setWordWrap(true);
  root->addWidget(note);

  auto* buttons = new QDialogButtonBox(this);
  buttons->addButton(QDialogButtonBox::Close);
  root->addWidget(buttons);

  connect(duplex_apply, &QPushButton::clicked, this, &RepeaterDialog::applyDuplex);
  connect(mode_apply, &QPushButton::clicked, this, &RepeaterDialog::applyToneMode);
  connect(tone_apply, &QPushButton::clicked, this, &RepeaterDialog::applyToneFreq);
  connect(tsql_apply, &QPushButton::clicked, this, &RepeaterDialog::applyTsqlFreq);
  connect(dtcs_apply, &QPushButton::clicked, this, &RepeaterDialog::applyDtcs);
  connect(sw_tone_, &QPushButton::toggled, this,
          [this](bool on) { session_->setRepeaterTone(on); });
  connect(sw_tsql_, &QPushButton::toggled, this,
          [this](bool on) { session_->setRepeaterTsql(on); });
  connect(sw_dtcs_, &QPushButton::toggled, this,
          [this](bool on) { session_->setDtcsFunction(on); });
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  connect(session_, &RadioSession::stateChanged, this,
          [this] { syncFromState(false); });

  syncFromState(true);
}

void RepeaterDialog::applyDuplex() {
  const auto dir = static_cast<uint8_t>(dir_->currentData().toInt());
  const auto hz = static_cast<uint32_t>(std::llround(offset_mhz_->value() * 1e6));
  if (!session_->setOffset(hz)) {
    readback_->setText("频差无效：须为 100Hz 整数倍且 ≤ 9.9999 MHz");
    return;
  }
  session_->setDuplex(dir);  // 先落频差再换方向，避免中途用旧偏移算 TX
  syncFromState(true);
}

void RepeaterDialog::applyToneMode() {
  const auto mode =
      static_cast<radio::core::civ::ToneSquelch>(tone_mode_->currentData().toInt());
  session_->setToneSquelch(mode);
  syncFromState(true);
}

void RepeaterDialog::applyToneFreq() {
  if (!session_->setToneFreq(static_cast<uint16_t>(tone_freq_->currentData().toInt()),
                             false))
    readback_->setText("中继音频率无效（上限 299.9 Hz）");
  syncFromState(true);
}

void RepeaterDialog::applyTsqlFreq() {
  if (!session_->setToneFreq(static_cast<uint16_t>(tsql_freq_->currentData().toInt()),
                             true))
    readback_->setText("TSQL 频率无效（上限 299.9 Hz）");
  syncFromState(true);
}

void RepeaterDialog::applyDtcs() {
  radio::core::civ::DtcsInfo info;
  info.code = dtcs_code_->currentData().toInt();
  info.rx_polarity = dtcs_rx_pol_->currentData().toInt();
  info.tx_polarity = dtcs_tx_pol_->currentData().toInt();
  if (!session_->setDtcsCode(info))
    readback_->setText("DTCS 码无效（须为三位八进制数字 000–777）");
  else
    syncFromState(true);
}

void RepeaterDialog::syncFromState(bool force) {
  const auto s = session_->store().snapshot();
  rx_label_->setText("RX  " + fmtFreq(s.freq_hz));
  if (s.duplex_raw == 0x11 || s.duplex_raw == 0x12) {
    tx_label_->setText(QString("TX  %1  %2%3")
                           .arg(fmtFreq(radio::core::effectiveTxHz(s)))
                           .arg(s.duplex_raw == 0x11 ? "−" : "+")
                           .arg(fmtOffset(s.offset_hz)));
  } else {
    tx_label_->setText("TX  " + fmtFreq(s.freq_hz) + "（同频）");
  }

  readback_->setText(
      QString("0x5D=%1 · 0x42=%2 · 0x43=%3 · 0x4B=%4 · 0x1B00=%5 · 0x1B01=%6 · "
              "0x1B02=%7 %8/%9")
          .arg(radio::core::civ::toneSquelchName(
              radio::core::civ::toneSquelchFromCode(
                  static_cast<uint8_t>(s.tone_squelch))
                  .value_or(radio::core::civ::ToneSquelch::Off)))
          .arg(s.repeater_tone ? "ON" : "OFF")
          .arg(s.tsql ? "ON" : "OFF")
          .arg(s.dtcs_on ? "ON" : "OFF")
          .arg(fmtTone(s.tone_decihz))
          .arg(fmtTone(s.tsql_decihz))
          .arg(fmtDtcs(s.dtcs_code))
          .arg(s.dtcs_rx_polarity ? "R" : "N")
          .arg(s.dtcs_tx_polarity ? "R" : "N"));

  if (synced_ && !force) return;  // 轮询只刷新双频/回读，不打断正在选的下拉项
  synced_ = true;

  selectData(dir_, s.duplex_raw == 0x11 ? 0x11
                                        : (s.duplex_raw == 0x12 ? 0x12 : 0x10));
  offset_mhz_->setValue(s.offset_hz / 1e6);
  selectData(tone_mode_, s.tone_squelch);
  selectData(tone_freq_, s.tone_decihz);
  selectData(tsql_freq_, s.tsql_decihz);
  selectData(dtcs_code_, s.dtcs_code);
  selectData(dtcs_rx_pol_, s.dtcs_rx_polarity);
  selectData(dtcs_tx_pol_, s.dtcs_tx_polarity);

  const auto block = [](QPushButton* b, bool on) {
    QSignalBlocker guard(b);
    b->setChecked(on);
  };
  block(sw_tone_, s.repeater_tone);
  block(sw_tsql_, s.tsql);
  block(sw_dtcs_, s.dtcs_on);
}

}  // namespace radio::app
