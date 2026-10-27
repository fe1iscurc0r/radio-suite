// SPDX-License-Identifier: AGPL-3.0-or-later
// radio-suite · app/set_mode_dialog.h — Set Mode 深设置树（0x1A 0x05 菜单项）
#pragma once

#include <QDialog>

class QComboBox;
class QLabel;
class QLineEdit;
class QSpinBox;

namespace radio::app {

class RadioSession;

class SetModeDialog : public QDialog {
  Q_OBJECT
 public:
  explicit SetModeDialog(RadioSession* session, QWidget* parent = nullptr);

 private:
  void onPresetChanged(int index);
  void readCurrent();
  void writeCurrent();
  void syncFromState();

  RadioSession* session_;
  QComboBox* preset_ = nullptr;
  QSpinBox* group_ = nullptr;
  QSpinBox* item_ = nullptr;
  QLineEdit* value_ = nullptr;
  QLabel* readback_ = nullptr;
  quint16 last_key_ = 0;
};

}  // namespace radio::app
