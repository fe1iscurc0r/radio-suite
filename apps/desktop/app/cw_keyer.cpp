// SPDX-License-Identifier: AGPL-3.0-or-later
#include "app/cw_keyer.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include "app/session.h"

namespace radio::app {

CwKeyerDialog::CwKeyerDialog(RadioSession* session, QWidget* parent)
    : QDialog(parent), session_(session) {
  setWindowTitle("CW Keyer");
  setMinimumWidth(420);

  auto* root = new QVBoxLayout(this);
  auto* form = new QFormLayout();

  text_ = new QPlainTextEdit(this);
  text_->setObjectName("cw_text");
  text_->setPlaceholderText("要发送的 CW 文本（自动大写，每帧 ≤30 字节）");
  text_->setPlainText("CQ CQ DE ");
  text_->setFixedHeight(90);
  root->addWidget(text_);

  wpm_ = new QSpinBox(this);
  wpm_->setObjectName("cw_wpm");
  wpm_->setRange(6, 48);
  wpm_->setValue(20);
  wpm_->setSuffix(" WPM");
  form->addRow("键速", wpm_);

  pitch_ = new QSpinBox(this);
  pitch_->setObjectName("cw_pitch");
  pitch_->setRange(300, 900);
  pitch_->setSingleStep(10);
  pitch_->setValue(600);
  pitch_->setSuffix(" Hz");
  form->addRow("侧音", pitch_);

  break_in_ = new QCheckBox("半插入（Break-in）", this);
  break_in_->setObjectName("cw_break_in");
  form->addRow(QString(), break_in_);

  break_delay_ = new QSpinBox(this);
  break_delay_->setObjectName("cw_break_delay");
  break_delay_->setRange(0, 255);
  break_delay_->setValue(26);
  form->addRow("半插入延时（原始电平）", break_delay_);
  root->addLayout(form);

  readback_ = new QLabel("—", this);
  readback_->setObjectName("cw_readback");
  readback_->setStyleSheet("color: #4dd0e1;");
  root->addWidget(readback_);

  auto* buttons = new QDialogButtonBox(this);
  auto* apply = buttons->addButton("应用设置", QDialogButtonBox::ApplyRole);
  auto* send = buttons->addButton("发送", QDialogButtonBox::ActionRole);
  auto* stop = buttons->addButton("停止", QDialogButtonBox::DestructiveRole);
  apply->setObjectName("cw_apply");
  send->setObjectName("cw_send");
  stop->setObjectName("cw_stop");
  buttons->addButton(QDialogButtonBox::Close);
  root->addWidget(buttons);

  connect(apply, &QPushButton::clicked, this, &CwKeyerDialog::applySettings);
  connect(send, &QPushButton::clicked, this, [this] {
    if (!session_->sendCwText(text_->toPlainText().toStdString(), wpm_->value()))
      readback_->setText("发送被拒（非业余频段或门禁未通过）");
  });
  connect(stop, &QPushButton::clicked, session_, &RadioSession::stopCw);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  connect(session_, &RadioSession::stateChanged, this, &CwKeyerDialog::syncFromState);

  session_->readCwSettings();
  syncFromState();
}

void CwKeyerDialog::applySettings() {
  session_->setKeySpeed(wpm_->value());
  session_->setCwPitch(pitch_->value());
  session_->setBreakIn(break_in_->isChecked());
  session_->setBreakInDelay(break_delay_->value());
  syncFromState();
}

void CwKeyerDialog::syncFromState() {
  const auto s = session_->store().snapshot();
  readback_->setText(
      QString("回读：键速 %1 WPM · 侧音 %2 Hz · Break-in %3 · 延时电平 %4")
          .arg(radio::core::civ::keySpeedLevelToWpm(s.cw_key_speed_level))
          .arg(radio::core::civ::cwPitchLevelToHz(s.cw_pitch_level))
          .arg(s.break_in ? "开" : "关")
          .arg(s.break_in_delay_level));
}

}  // namespace radio::app
