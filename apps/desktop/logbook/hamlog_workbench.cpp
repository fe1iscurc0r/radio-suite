// SPDX-License-Identifier: AGPL-3.0-or-later
// HamLog 工作台 v2 实现——功能面对齐原版 R1.0.0（对照表见 hamlog_workbench.h）
#include "logbook/hamlog_workbench.h"

#include <QDesktopServices>
#include <QFileDialog>
#include <QFile>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QUrl>
#include <QVBoxLayout>

#include "logbook/qso_edit_dialog.h"

namespace radio::logbook {

HamLogWorkbench::HamLogWorkbench(QWidget* parent) : QWidget(parent) {
  auto* root = new QVBoxLayout(this);

  auto* head = new QHBoxLayout();
  title_ = new QLabel(this);
  head->addWidget(title_, 1);
  auto* reload = new QPushButton("刷新", this);
  connect(reload, &QPushButton::clicked, this, &HamLogWorkbench::refresh);
  head->addWidget(reload);
  auto* settings = new QPushButton("设置", this);
  settings->setToolTip("操作员信息（my_callsign/my_name/my_qth/my_grid，直写 HamLog settings 表）");
  connect(settings, &QPushButton::clicked, this, &HamLogWorkbench::onSettings);
  head->addWidget(settings);
  auto* adif = new QPushButton("ADIF 导出", this);
  connect(adif, &QPushButton::clicked, this, &HamLogWorkbench::onExport);
  head->addWidget(adif);
  auto* close = new QPushButton("关闭", this);
  close->setToolTip("关闭工作台窗口");
  connect(close, &QPushButton::clicked, this, &QWidget::close);
  head->addWidget(close);
  root->addLayout(head);

  auto* srow = new QHBoxLayout();
  srow->addWidget(new QLabel("搜索", this));
  search_ = new QLineEdit(this);
  // 原版 placeholder 语义（HAMLOG GUI.py:1323）：呼号、QTH、设备、备注四字段模糊
  search_->setPlaceholderText("搜索呼号、QTH、设备、备注...");
  connect(search_, &QLineEdit::returnPressed, this, &HamLogWorkbench::onSearch);
  srow->addWidget(search_, 1);
  auto* search_btn = new QPushButton("搜索", this);
  search_btn->setToolTip("Callsign/QTH/Device/Remarks 四字段模糊匹配（原版 AutoDeal.search 语义）");
  connect(search_btn, &QPushButton::clicked, this, &HamLogWorkbench::onSearch);
  srow->addWidget(search_btn);
  root->addLayout(srow);

  table_ = new QTableWidget(0, 11, this);
  table_->setHorizontalHeaderLabels({"呼号", "频率 MHz", "模式", "日期", "时间",
                                     "RST发", "RST收", "QTH", "设备", "QSL发",
                                     "QSL收"});
  table_->horizontalHeader()->setStretchLastSection(true);
  table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  table_->setSelectionBehavior(QAbstractItemView::SelectRows);
  table_->setAlternatingRowColors(true);
  root->addWidget(table_, 1);

  auto* orow = new QHBoxLayout();
  auto* add = new QPushButton("新增", this);
  connect(add, &QPushButton::clicked, this, &HamLogWorkbench::onAdd);
  orow->addWidget(add);
  auto* edit = new QPushButton("修改选中", this);
  connect(edit, &QPushButton::clicked, this, &HamLogWorkbench::onEdit);
  orow->addWidget(edit);
  auto* tmpl = new QPushButton("以模板添加", this);
  tmpl->setToolTip("以选中记录预填（原版：补录同场通联，额外继承日期）");
  connect(tmpl, &QPushButton::clicked, this, &HamLogWorkbench::onAddFromTemplate);
  orow->addWidget(tmpl);
  auto* del = new QPushButton("删除选中", this);
  connect(del, &QPushButton::clicked, this, &HamLogWorkbench::onRemove);
  orow->addWidget(del);
  orow->addStretch(1);
  orow->addWidget(new QLabel("QRZ:", this));
  qrz_call_ = new QLineEdit(this);
  qrz_call_->setPlaceholderText("呼号");
  qrz_call_->setMaximumWidth(120);
  orow->addWidget(qrz_call_);
  auto* qrz = new QPushButton("QRZ 查询", this);
  qrz->setToolTip("浏览器打开 QRZ.com 呼号页（原版 QRZ_Lookup_Dialog 的轻量版）");
  connect(qrz, &QPushButton::clicked, this, &HamLogWorkbench::onQrzLookup);
  orow->addWidget(qrz);
  root->addLayout(orow);

  stat_ = new QLabel(this);
  stat_->setStyleSheet("color:#8fe;");
  root->addWidget(stat_);

  refresh();
}

void HamLogWorkbench::refresh() {
  const bool ok = db_.available();
  title_->setText(ok ? QString("HamLog 工作台 v2（C++ 对齐原版功能面）· %1")
                           .arg(db_.defaultDbPath())
                     : "HamLog 库未找到（先运行一次 HamLog）");
  if (!ok) return;
  const auto qsos = db_.search(search_ ? search_->text() : QString());
  table_->setRowCount(static_cast<int>(qsos.size()));
  for (int r = 0; r < static_cast<int>(qsos.size()); ++r) {
    const Qso& q = qsos[static_cast<size_t>(r)];
    table_->setItem(r, 0, new QTableWidgetItem(q.callsign));
    table_->setItem(r, 1, new QTableWidgetItem(QString::number(q.freq_mhz, 'f', 4)));
    table_->setItem(r, 2, new QTableWidgetItem(q.mode));
    table_->setItem(r, 3, new QTableWidgetItem(
                               QString("%1-%2-%3").arg(q.year).arg(q.month, 2, 10,
                                                                    QChar('0'))
                                   .arg(q.day, 2, 10, QChar('0'))));
    table_->setItem(r, 4, new QTableWidgetItem(q.time_hhmm));
    table_->setItem(r, 5, new QTableWidgetItem(q.rst_sent));
    table_->setItem(r, 6, new QTableWidgetItem(q.rst_rcvd));
    table_->setItem(r, 7, new QTableWidgetItem(q.qth));
    table_->setItem(r, 8, new QTableWidgetItem(q.device));
    table_->setItem(r, 9, new QTableWidgetItem(q.qsl_send));
    table_->setItem(r, 10, new QTableWidgetItem(q.qsl_rx));
    // QSL 卡债高亮（hamlog_adapter 同语义）
    if (q.qsl_send.isEmpty())
      table_->item(r, 9)->setForeground(QColor(255, 160, 120));
  }
  statBand(qsos);
}

void HamLogWorkbench::statBand(const std::vector<Qso>& qsos) {
  int hf = 0, v_u = 0, other = 0, qsl_debt = 0;
  for (const auto& q : qsos) {
    if (q.freq_mhz >= 1.5 && q.freq_mhz <= 30) ++hf;
    else if (q.freq_mhz >= 50) ++v_u;
    else ++other;
    if (q.qsl_send.isEmpty()) ++qsl_debt;
  }
  stat_->setText(QString("共 %1 条 · HF %2 · V/UHF+ %3 · 其他 %4 · QSL 卡债 %5（橙标）")
                     .arg(qsos.size()).arg(hf).arg(v_u).arg(other).arg(qsl_debt));
}

int64_t HamLogWorkbench::selectedId() const {
  const int r = table_->currentRow();
  if (r < 0) return 0;
  const auto* item = table_->item(r, 0);
  if (!item) return 0;
  // 呼号+日期在表内不唯一；按当前搜索结果回查 id（search 保持同序）
  const auto qsos = db_.search(search_->text());
  if (r >= static_cast<int>(qsos.size())) return 0;
  return qsos[static_cast<size_t>(r)].id;
}

void HamLogWorkbench::onSearch() { refresh(); }

void HamLogWorkbench::onAdd() {
  Qso blank;
  QsoEditDialog dlg(db_, QsoEditDialog::Mode::Add, blank, this);
  if (dlg.exec() == QDialog::Accepted && db_.insert(dlg.value())) {
    refresh();
    stat_->setText("已新增：" + dlg.value().callsign);
  }
}

void HamLogWorkbench::onEdit() {
  const int64_t id = selectedId();
  if (id == 0) {
    stat_->setText("先选中一条记录");
    return;
  }
  auto cur = db_.getById(id);
  if (!cur) return;
  QsoEditDialog dlg(db_, QsoEditDialog::Mode::Edit, *cur, this);
  if (dlg.exec() == QDialog::Accepted && db_.update(dlg.value())) refresh();
}

void HamLogWorkbench::onAddFromTemplate() {
  const int64_t id = selectedId();
  if (id == 0) {
    stat_->setText("先选中一条作为模板");
    return;
  }
  auto cur = db_.getById(id);
  if (!cur) return;
  cur->id = 0;  // 模板语义：新记录（原版：额外继承日期，补录同场通联）
  QsoEditDialog dlg(db_, QsoEditDialog::Mode::Add, *cur, this);
  if (dlg.exec() == QDialog::Accepted && db_.insert(dlg.value())) {
    refresh();
    stat_->setText("已按模板新增：" + dlg.value().callsign);
  }
}

void HamLogWorkbench::onRemove() {
  const int64_t id = selectedId();
  if (id == 0) {
    stat_->setText("先选中一条");
    return;
  }
  if (QMessageBox::question(this, "删除", "确认删除该条记录？") !=
      QMessageBox::Yes)
    return;
  if (db_.remove(id)) {
    refresh();
    stat_->setText("已删除");
  } else {
    stat_->setText(db_.lastError());
  }
}

void HamLogWorkbench::onSettings() {
  // my_* 四项（原版 SettingsDialog 的 station 组）：直写 settings 表同 key
  struct Item { const char* key; const char* label; };
  const Item items[] = {{"my_callsign", "操作员呼号"},
                        {"my_name", "操作员姓名"},
                        {"my_qth", "常用 QTH"},
                        {"my_grid", "网格坐标"}};
  bool changed = false;
  for (const Item& it : items) {
    const QString cur = db_.getSetting(it.key);
    const QString v = QInputDialog::getText(this, "HamLog 设置", it.label,
                                            QLineEdit::Normal, cur);
    if (!db_.setSetting(it.key, v)) break;
    changed = true;
  }
  if (changed) refresh();
}

void HamLogWorkbench::onQrzLookup() {
  const QString call = qrz_call_->text().trimmed().toUpper();
  if (call.isEmpty()) {
    stat_->setText("先输入呼号");
    return;
  }
  QDesktopServices::openUrl(QUrl("https://www.qrz.com/lookup/" + call));
}

void HamLogWorkbench::onExport() {
  const auto qsos = db_.search(search_->text());
  if (qsos.empty()) {
    stat_->setText("无记录可导出");
    return;
  }
  const QString path =
      QFileDialog::getSaveFileName(this, "ADIF 导出", "hamlog.adi", "ADIF (*.adi)");
  if (path.isEmpty()) return;
  QFile f(path);
  if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return;
  f.write(toAdif(qsos, db_.myCallsign()).toUtf8());
  f.close();
  stat_->setText(QString("已导出 %1 条 → %2").arg(qsos.size()).arg(path));
}

}  // namespace radio::logbook
