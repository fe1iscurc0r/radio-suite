// SPDX-License-Identifier: AGPL-3.0-or-later
// radio-suite · logbook/qso_edit_dialog.h — QSO 完整字段编辑（对齐原版 LogDialog）
// 新增 / 修改 / 以模板预填 三用；字段集 = 原版 LogDialog（HAMLOG GUI.py:232+）。
#pragma once

#include <QDialog>

#include "logbook/logbook.h"  // Qso 值成员需要完整类型（HamLogDb 同理）

class QComboBox;
class QDateEdit;
class QLineEdit;
class QTimeEdit;

namespace radio::logbook {

class QsoEditDialog : public QDialog {
  Q_OBJECT
 public:
  enum class Mode { Add, Edit };
  QsoEditDialog(HamLogDb& db, Mode mode, const Qso& initial,
                QWidget* parent = nullptr);

  Qso value() const { return qso_; }

 private slots:
  void onSave();

 private:
  HamLogDb& db_;
  Mode mode_ = Mode::Add;
  Qso qso_;
  QLineEdit* callsign_ = nullptr;
  QLineEdit* freq_ = nullptr;
  QDateEdit* date_ = nullptr;
  QTimeEdit* time_ = nullptr;
  QComboBox* mode_box_ = nullptr;  // 与枚举 Mode 区分（mode_ 曾撞名枚举类型）
  QLineEdit* rst_sent_ = nullptr;
  QLineEdit* rst_rcvd_ = nullptr;
  QLineEdit* power_self_ = nullptr;
  QLineEdit* power_side_ = nullptr;
  QLineEdit* qth_ = nullptr;
  QLineEdit* device_ = nullptr;
  QLineEdit* remarks_ = nullptr;
};

}  // namespace radio::logbook
