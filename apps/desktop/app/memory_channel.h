// SPDX-License-Identifier: AGPL-3.0-or-later
// radio-suite · app/memory_channel.h — 电台记忆信道（0x1A 0x00 / 0x08）
#pragma once

#include <QDialog>

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QSpinBox;

namespace radio::app {

class RadioSession;

class MemoryChannelDialog : public QDialog {
  Q_OBJECT
 public:
  explicit MemoryChannelDialog(RadioSession* session, QWidget* parent = nullptr);

 private:
  void readChannel();
  void writeChannel();
  void syncFromState();

  RadioSession* session_;
  QSpinBox* group_ = nullptr;
  QSpinBox* channel_ = nullptr;
  QDoubleSpinBox* freq_mhz_ = nullptr;
  QComboBox* mode_ = nullptr;
  QSpinBox* filter_ = nullptr;
  QSpinBox* scan_ = nullptr;
  QSpinBox* data_mode_ = nullptr;
  QSpinBox* tone_mode_ = nullptr;
  QLineEdit* name_ = nullptr;
  QLabel* readback_ = nullptr;
  int pending_channel_ = -1;
  bool apply_pending_ = false;  // 仅读/写动作完成后灌表单，避免轮询回读覆盖用户输入
};

}  // namespace radio::app
