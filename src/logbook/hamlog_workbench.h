// SPDX-License-Identifier: AGPL-3.0-or-later
// radio-suite · logbook/hamlog_workbench.h — HamLog C++ 工作台（师傅点名重构）
// 同一份 Log.db（与 PyQt6 HamLog、scratchpad hamlog_adapter 三方互通）：
// 全表浏览 / 呼号·波段搜索 / 按波段统计 / ADIF 导出。
#pragma once

#include <QWidget>

#include "logbook/logbook.h"

class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;

namespace radio::logbook {

class HamLogWorkbench : public QWidget {
  Q_OBJECT
 public:
  explicit HamLogWorkbench(QWidget* parent = nullptr);

 private slots:
  void onSearch();
  void onExport();
  void refresh();

 private:
  void statBand(const std::vector<Qso>& qsos);

  HamLogDb db_;
  QTableWidget* table_ = nullptr;
  QLineEdit* search_ = nullptr;
  QLabel* stat_ = nullptr;
  QLabel* title_ = nullptr;
};

}  // namespace radio::logbook
