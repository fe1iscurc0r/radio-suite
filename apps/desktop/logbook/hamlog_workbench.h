// SPDX-License-Identifier: AGPL-3.0-or-later
// radio-suite · logbook/hamlog_workbench.h — HamLog 工作台 v2（对齐原版功能面）
// 原版（R1.0.0 HAMLOG GUI.py）功能对照：
//   四字段模糊搜索（Callsign/QTH/Device/Remarks）✓ · LogDialog 完整编辑 ✓ ·
//   以模板添加 ✓ · 删除 ✓ · 设置（my_* 四项直写 settings 表）✓ ·
//   QRZ.com 查询（浏览器跳转版）✓ · ADIF 导出 ✓ · 关闭键 ✓ ·
//   LoTW 上传 / 字体 / 代理：共用 `ui_common` 对话框，已实现（非假件）。
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
  void onAdd();               // 新增（空模板）
  void onEdit();              // 修改选中
  void onAddFromTemplate();   // 以选中记录预填新增（原版"补录同场通联"）
  void onRemove();            // 删除选中
  void onSettings();          // my_* 设置编辑
  void onQrzLookup();         // QRZ.com 呼号查询（浏览器跳转）
  void refresh();

 private:
  void statBand(const std::vector<Qso>& qsos);
  int64_t selectedId() const;

  HamLogDb db_;
  QTableWidget* table_ = nullptr;
  QLineEdit* search_ = nullptr;
  QLineEdit* qrz_call_ = nullptr;
  QLabel* stat_ = nullptr;
  QLabel* title_ = nullptr;
};

}  // namespace radio::logbook
