// SPDX-License-Identifier: AGPL-3.0-or-later
// radio-suite · app/cw_keyer.h — CW Keyer（0x17 文本 / 0x14 电平 / 0x16 0x47）
#pragma once

#include <QDialog>

class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QSpinBox;
class QCheckBox;

namespace radio::app {

class RadioSession;

class CwKeyerDialog : public QDialog {
  Q_OBJECT
 public:
  explicit CwKeyerDialog(RadioSession* session, QWidget* parent = nullptr);

 private:
  void applySettings();
  void syncFromState();

  RadioSession* session_;
  QPlainTextEdit* text_ = nullptr;
  QSpinBox* wpm_ = nullptr;
  QSpinBox* pitch_ = nullptr;
  QCheckBox* break_in_ = nullptr;
  QSpinBox* break_delay_ = nullptr;
  QLabel* readback_ = nullptr;
};

}  // namespace radio::app
