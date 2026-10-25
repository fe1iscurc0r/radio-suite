// SPDX-License-Identifier: AGPL-3.0-or-later
#include "logbook/qso_edit_dialog.h"

#include <QComboBox>
#include <QDateEdit>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTimeEdit>
#include <QVBoxLayout>

#include "logbook.h"  // 完整类型（HamLogDb/Qso 定义）

namespace radio::logbook {
namespace {
// 原版 LogDialog 的模式集（HAMLOG GUI.py 模式下拉）
const char* kModes[] = {"SSB", "CW", "FM", "AM", "RTTY", "PSK", "FT8", "FT4"};
}  // namespace

QsoEditDialog::QsoEditDialog(HamLogDb& db, Mode mode, const Qso& initial,
                             QWidget* parent)
    : QDialog(parent), db_(db), mode_(mode), qso_(initial) {
  setWindowTitle(mode == Mode::Add ? "新增日志记录" : "修改日志记录");
  auto* root = new QVBoxLayout(this);
  auto* form = new QFormLayout();

  callsign_ = new QLineEdit(qso_.callsign, this);
  form->addRow("对方呼号*:", callsign_);
  freq_ = new QLineEdit(qso_.freq_mhz > 0 ? QString::number(qso_.freq_mhz, 'f', 4)
                                          : QString(), this);
  form->addRow("频率 MHz:", freq_);
  date_ = new QDateEdit(QDate(qso_.year ? qso_.year : QDate::currentDate().year(),
                              qso_.month ? qso_.month : QDate::currentDate().month(),
                              qso_.day ? qso_.day : QDate::currentDate().day()), this);
  date_->setDisplayFormat("yyyy-MM-dd");
  date_->setCalendarPopup(true);
  form->addRow("日期*:", date_);
  time_ = new QTimeEdit(QTime::fromString(
      qso_.time_hhmm.isEmpty() ? QTime::currentTime().toString("hhmm")
                               : qso_.time_hhmm, "hhmm"));
  time_->setDisplayFormat("HH:mm");
  form->addRow("时间 UTC*:", time_);
  mode_box_ = new QComboBox(this);
  for (const char* m : kModes) mode_box_->addItem(m);
  const int mi = mode_box_->findText(qso_.mode.isEmpty() ? "SSB" : qso_.mode);
  mode_box_->setCurrentIndex(mi < 0 ? 0 : mi);
  form->addRow("模式*:", mode_box_);

  rst_sent_ = new QLineEdit(qso_.rst_sent.isEmpty() ? "59" : qso_.rst_sent, this);
  form->addRow("我给对方*:", rst_sent_);
  rst_rcvd_ = new QLineEdit(qso_.rst_rcvd.isEmpty() ? "59" : qso_.rst_rcvd, this);
  form->addRow("对方给我*:", rst_rcvd_);
  qth_ = new QLineEdit(qso_.qth, this);
  form->addRow("QTH:", qth_);
  device_ = new QLineEdit(qso_.device.isEmpty() ? "IC-705" : qso_.device, this);
  form->addRow("设备:", device_);
  remarks_ = new QLineEdit(qso_.remarks, this);
  form->addRow("备注:", remarks_);
  root->addLayout(form);

  auto* row = new QHBoxLayout();
  row->addStretch(1);
  auto* save = new QPushButton("保存", this);
  connect(save, &QPushButton::clicked, this, &QsoEditDialog::onSave);
  row->addWidget(save);
  auto* cancel = new QPushButton("取消", this);
  connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
  row->addWidget(cancel);
  root->addLayout(row);
}

void QsoEditDialog::onSave() {
  const QString call = callsign_->text().trimmed().toUpper();
  if (call.isEmpty()) {
    setWindowTitle("对方呼号必填！");
    return;
  }
  bool ok = false;
  const double f = freq_->text().toDouble(&ok);
  qso_.callsign = call;
  qso_.freq_mhz = ok ? f : 0.0;
  qso_.year = date_->date().year();
  qso_.month = date_->date().month();
  qso_.day = date_->date().day();
  qso_.time_hhmm = time_->time().toString("hhmm");
  qso_.mode = mode_box_->currentText();
  qso_.rst_sent = rst_sent_->text();
  qso_.rst_rcvd = rst_rcvd_->text();
  qso_.qth = qth_->text();
  qso_.device = device_->text();
  qso_.remarks = remarks_->text();
  accept();
}

}  // namespace radio::logbook
