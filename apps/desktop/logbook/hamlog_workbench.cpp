// SPDX-License-Identifier: AGPL-3.0-or-later
#include "logbook/hamlog_workbench.h"

#include <QFileDialog>
#include <QFile>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

namespace radio::logbook {

HamLogWorkbench::HamLogWorkbench(QWidget* parent) : QWidget(parent) {
  auto* root = new QVBoxLayout(this);

  auto* head = new QHBoxLayout();
  title_ = new QLabel(this);
  head->addWidget(title_, 1);
  auto* adif = new QPushButton("ADIF 导出", this);
  connect(adif, &QPushButton::clicked, this, &HamLogWorkbench::onExport);
  head->addWidget(adif);
  auto* reload = new QPushButton("刷新", this);
  connect(reload, &QPushButton::clicked, this, &HamLogWorkbench::refresh);
  head->addWidget(reload);
  root->addLayout(head);

  auto* srow = new QHBoxLayout();
  srow->addWidget(new QLabel("搜索（呼号/频率）", this));
  search_ = new QLineEdit(this);
  search_->setPlaceholderText("如 BG5 或 145，回车执行");
  connect(search_, &QLineEdit::returnPressed, this, &HamLogWorkbench::onSearch);
  srow->addWidget(search_, 1);
  root->addLayout(srow);

  table_ = new QTableWidget(0, 9, this);
  table_->setHorizontalHeaderLabels({"呼号", "频率 MHz", "模式", "日期", "时间",
                                     "RST发", "RST收", "QSL发", "QSL收"});
  table_->horizontalHeader()->setStretchLastSection(true);
  table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  table_->setAlternatingRowColors(true);
  root->addWidget(table_, 1);

  stat_ = new QLabel(this);
  stat_->setStyleSheet("color:#8fe;");
  root->addWidget(stat_);

  refresh();
}

void HamLogWorkbench::refresh() {
  const bool ok = db_.available();
  title_->setText(ok ? QString("HamLog 工作台（C++ 重构）· %1")
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
    table_->setItem(r, 7, new QTableWidgetItem(q.qsl_send));
    table_->setItem(r, 8, new QTableWidgetItem(q.qsl_rx));
    // QSL 卡债高亮（hamlog_adapter 同语义）
    if (q.qsl_send.isEmpty())
      table_->item(r, 7)->setForeground(QColor(255, 160, 120));
  }
  statBand(qsos);
}

void HamLogWorkbench::statBand(const std::vector<Qso>& qsos) {
  // 按频率段粗统计（HAMLog Freq 为 MHz）
  int hf = 0, v_u = 0, other = 0;
  for (const auto& q : qsos) {
    if (q.freq_mhz >= 1.5 && q.freq_mhz <= 30) ++hf;
    else if (q.freq_mhz >= 50) ++v_u;
    else ++other;
  }
  stat_->setText(QString("共 %1 条（搜索结果）· HF %2 · V/UHF 及以上 %3 · 其他 %4 · "
                         "橙色=QSL 未发")
                     .arg(qsos.size()).arg(hf).arg(v_u).arg(other));
}

void HamLogWorkbench::onSearch() { refresh(); }

void HamLogWorkbench::onExport() {
  const auto qsos = db_.search(search_->text());
  if (qsos.empty()) return;
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
