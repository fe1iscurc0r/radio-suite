// SPDX-License-Identifier: AGPL-3.0-or-later
// radio-suite · app/repeater_dialog.h — 中继频差 + 亚音（0x0F/0x0D/0x16/0x1B）
#pragma once

#include <QDialog>

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;

namespace radio::app {

class RadioSession;

class RepeaterDialog : public QDialog {
  Q_OBJECT
 public:
  explicit RepeaterDialog(RadioSession* session, QWidget* parent = nullptr);

 private:
  void applyDuplex();
  void applyToneMode();
  void applyToneFreq();
  void applyTsqlFreq();
  void applyDtcs();
  void syncFromState(bool force);

  RadioSession* session_;
  QComboBox* dir_ = nullptr;
  QDoubleSpinBox* offset_mhz_ = nullptr;
  QLabel* rx_label_ = nullptr;
  QLabel* tx_label_ = nullptr;
  QComboBox* tone_mode_ = nullptr;
  QComboBox* tone_freq_ = nullptr;
  QComboBox* tsql_freq_ = nullptr;
  QComboBox* dtcs_code_ = nullptr;
  QComboBox* dtcs_rx_pol_ = nullptr;
  QComboBox* dtcs_tx_pol_ = nullptr;
  QPushButton* sw_tone_ = nullptr;
  QPushButton* sw_tsql_ = nullptr;
  QPushButton* sw_dtcs_ = nullptr;
  QLabel* readback_ = nullptr;
  bool synced_ = false;
};

}  // namespace radio::app
