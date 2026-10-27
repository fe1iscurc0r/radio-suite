// SPDX-License-Identifier: AGPL-3.0-or-later
#include "app/memory_channel.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include "app/session.h"

namespace radio::app {

MemoryChannelDialog::MemoryChannelDialog(RadioSession* session, QWidget* parent)
    : QDialog(parent), session_(session) {
  setWindowTitle("Memory Channel — 记忆信道");
  setMinimumWidth(440);

  auto* root = new QVBoxLayout(this);
  auto* form = new QFormLayout();

  group_ = new QSpinBox(this);
  group_->setObjectName("mem_group");
  group_->setRange(0, 99);
  group_->setValue(1);
  form->addRow("记忆组", group_);

  channel_ = new QSpinBox(this);
  channel_->setObjectName("mem_channel");
  channel_->setRange(0, 99);
  channel_->setValue(1);
  form->addRow("信道", channel_);

  freq_mhz_ = new QDoubleSpinBox(this);
  freq_mhz_->setObjectName("mem_freq");
  freq_mhz_->setRange(0.1, 1450.0);
  freq_mhz_->setDecimals(6);
  freq_mhz_->setValue(14.27);
  form->addRow("频率 (MHz)", freq_mhz_);

  mode_ = new QComboBox(this);
  mode_->setObjectName("mem_mode");
  for (radio::core::civ::Mode m : {radio::core::civ::Mode::LSB,
                                   radio::core::civ::Mode::USB,
                                   radio::core::civ::Mode::AM,
                                   radio::core::civ::Mode::CW,
                                   radio::core::civ::Mode::RTTY,
                                   radio::core::civ::Mode::FM,
                                   radio::core::civ::Mode::WFM})
    mode_->addItem(radio::core::civ::modeName(m), static_cast<int>(m));
  mode_->setCurrentIndex(1);  // USB
  form->addRow("模式", mode_);

  filter_ = new QSpinBox(this);
  filter_->setObjectName("mem_filter");
  filter_->setRange(0, 3);
  filter_->setValue(1);
  form->addRow("滤波器", filter_);

  scan_ = new QSpinBox(this);
  scan_->setObjectName("mem_scan");
  scan_->setRange(0, 255);
  form->addRow("Scan", scan_);

  data_mode_ = new QSpinBox(this);
  data_mode_->setObjectName("mem_data");
  data_mode_->setRange(0, 1);
  form->addRow("DATA", data_mode_);

  tone_mode_ = new QSpinBox(this);
  tone_mode_->setObjectName("mem_tone");
  tone_mode_->setRange(0, 15);
  form->addRow("Tone", tone_mode_);

  name_ = new QLineEdit(this);
  name_->setObjectName("mem_name");
  name_->setMaxLength(10);
  name_->setPlaceholderText("≤10 ASCII");
  form->addRow("名称", name_);
  root->addLayout(form);

  auto* hint = new QLabel(
      "0x1A 0x00 读：<信道 BCD2>；写：信道 + 26 字节内容块。组选择 0x08 0xA0，信道 0x08。", this);
  hint->setStyleSheet("color: #9e9e9e;");
  hint->setWordWrap(true);
  root->addWidget(hint);

  readback_ = new QLabel("—", this);
  readback_->setObjectName("mem_readback");
  readback_->setStyleSheet("color: #4dd0e1;");
  readback_->setWordWrap(true);
  root->addWidget(readback_);

  auto* buttons = new QDialogButtonBox(this);
  auto* read = buttons->addButton("读取", QDialogButtonBox::ActionRole);
  auto* write = buttons->addButton("写入", QDialogButtonBox::ActionRole);
  auto* sel_group = buttons->addButton("选组", QDialogButtonBox::ActionRole);
  auto* sel_ch = buttons->addButton("选信道", QDialogButtonBox::ActionRole);
  read->setObjectName("mem_read");
  write->setObjectName("mem_write");
  buttons->addButton(QDialogButtonBox::Close);
  root->addWidget(buttons);

  connect(read, &QPushButton::clicked, this, &MemoryChannelDialog::readChannel);
  connect(write, &QPushButton::clicked, this, &MemoryChannelDialog::writeChannel);
  connect(sel_group, &QPushButton::clicked, this,
          [this] { session_->selectMemoryGroup(group_->value()); });
  connect(sel_ch, &QPushButton::clicked, this,
          [this] { session_->selectMemoryChannel(channel_->value()); });
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  connect(session_, &RadioSession::stateChanged, this,
          &MemoryChannelDialog::syncFromState);
}

void MemoryChannelDialog::readChannel() {
  pending_channel_ = channel_->value();
  apply_pending_ = true;
  session_->readMemoryChannel(pending_channel_);
  syncFromState();
}

void MemoryChannelDialog::writeChannel() {
  radio::core::civ::MemoryChannel m;
  m.channel = channel_->value();
  m.scan = scan_->value();
  m.freq_hz = static_cast<uint64_t>(freq_mhz_->value() * 1e6 + 0.5);
  m.mode = static_cast<radio::core::civ::Mode>(mode_->currentData().toInt());
  m.filter = filter_->value();
  m.data_mode = data_mode_->value();
  m.tone_mode = tone_mode_->value();
  m.name = name_->text().toStdString();
  pending_channel_ = m.channel;
  apply_pending_ = true;
  session_->writeMemoryChannel(m);
  syncFromState();
}

void MemoryChannelDialog::syncFromState() {
  if (!apply_pending_) return;
  const auto s = session_->store().snapshot();
  if (!s.memory_channel) {
    readback_->setText("回读：尚无信道内容（未读取或电台 NAK）");
    return;
  }
  const auto& m = *s.memory_channel;
  if (m.channel != pending_channel_) return;
  apply_pending_ = false;
  readback_->setText(
      QString("回读：CH%1 %2 · %3 · filter %4 · DATA %5 · Tone %6 · %7")
          .arg(m.channel)
          .arg(m.freq_hz / 1e6, 0, 'f', 6)
          .arg(radio::core::civ::modeName(m.mode))
          .arg(m.filter)
          .arg(m.data_mode)
          .arg(m.tone_mode)
          .arg(QString::fromStdString(m.name)));

  // 回读后把内容灌回表单（写后确认也是同一路径）
  freq_mhz_->setValue(m.freq_hz / 1e6);
  mode_->setCurrentIndex(mode_->findData(static_cast<int>(m.mode)));
  filter_->setValue(m.filter);
  scan_->setValue(m.scan);
  data_mode_->setValue(m.data_mode);
  tone_mode_->setValue(m.tone_mode);
  if (!m.name.empty()) name_->setText(QString::fromStdString(m.name));
}

}  // namespace radio::app
