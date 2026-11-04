// SPDX-License-Identifier: AGPL-3.0-or-later
// radio-suite · app/self_check_dialog.h — 链路与权限台账（手机上直接看真相）
#pragma once

#include <functional>

#include <QDialog>

#include "app/device_report.h"

class QGridLayout;

namespace radio::app {

// 台账页不自己猜任何状态：所有事实由 provider 现场取（主窗传真机状态，
// 用例传合成数据，所以 Android 的措辞在桌面上也能跑断言）。
class SelfCheckDialog : public QDialog {
  Q_OBJECT
 public:
  explicit SelfCheckDialog(std::function<ReportInput()> provider,
                           QWidget* parent = nullptr);

  int rowCount() const { return static_cast<int>(rows_.size()); }
  QString noteAt(int row) const;
  void refresh();

 private:
  void render();

  std::function<ReportInput()> provider_;
  QGridLayout* grid_ = nullptr;
  QVector<ReportRow> rows_;
  ReportInput last_input_;
};

}  // namespace radio::app
