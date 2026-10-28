// SPDX-License-Identifier: AGPL-3.0-or-later
#include "app/aprs_messages_dialog.h"

#include <QDateTime>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>

#include "aprs/store.h"

namespace radio::app {
namespace {

QString when(int64_t ms) {
  return QDateTime::fromMSecsSinceEpoch(ms).toString("MM-dd hh:mm:ss");
}

QTableWidget* makeTable(const QStringList& headers) {
  auto* t = new QTableWidget(0, headers.size());
  t->setHorizontalHeaderLabels(headers);
  t->horizontalHeader()->setStretchLastSection(true);
  t->setEditTriggers(QAbstractItemView::NoEditTriggers);
  t->setSelectionBehavior(QAbstractItemView::SelectRows);
  t->verticalHeader()->setVisible(false);
  return t;
}

}  // namespace

AprsMessagesDialog::AprsMessagesDialog(QWidget* parent) : QDialog(parent) {
  setWindowTitle("APRS 消息");
  resize(720, 420);
  auto* root = new QVBoxLayout(this);

  summary_ = new QLabel("未读 0 / 收 0 / 发 0", this);
  root->addWidget(summary_);

  auto* tabs = new QTabWidget(this);
  inbox_ = makeTable({"时间", "来自", "行号", "内容"});
  inbox_->setColumnWidth(0, 140);
  inbox_->setColumnWidth(1, 100);
  inbox_->setColumnWidth(2, 50);
  tabs->addTab(inbox_, "收件箱");
  outbox_ = makeTable({"时间", "发给", "ID", "尝试", "状态", "内容"});
  outbox_->setColumnWidth(0, 140);
  outbox_->setColumnWidth(1, 100);
  outbox_->setColumnWidth(2, 50);
  outbox_->setColumnWidth(3, 50);
  outbox_->setColumnWidth(4, 90);
  tabs->addTab(outbox_, "发件箱");
  root->addWidget(tabs, 1);

  auto* compose = new QHBoxLayout();
  to_edit_ = new QLineEdit(this);
  to_edit_->setPlaceholderText("目标呼号");
  to_edit_->setMaximumWidth(120);
  compose->addWidget(new QLabel("发给", this));
  compose->addWidget(to_edit_);
  text_edit_ = new QLineEdit(this);
  text_edit_->setPlaceholderText("消息内容（最多 67 字符/行）");
  text_edit_->setMaxLength(67);
  compose->addWidget(text_edit_, 1);
  send_btn_ = new QPushButton("发送", this);
  compose->addWidget(send_btn_);
  root->addLayout(compose);

  auto* buttons = new QHBoxLayout();
  auto* read_btn = new QPushButton("全部标为已读", this);
  buttons->addWidget(read_btn);
  buttons->addStretch(1);
  auto* close_btn = new QPushButton("关闭", this);
  buttons->addWidget(close_btn);
  root->addLayout(buttons);

  connect(send_btn_, &QPushButton::clicked, this,
          &AprsMessagesDialog::onSend);
  connect(text_edit_, &QLineEdit::returnPressed, this,
          &AprsMessagesDialog::onSend);
  connect(read_btn, &QPushButton::clicked, this,
          [this] { emit markAllReadRequested(); });
  connect(close_btn, &QPushButton::clicked, this, &QDialog::hide);
}

void AprsMessagesDialog::setMyCall(const QString& call) {
  if (!call.isEmpty()) my_call_ = call;
}

void AprsMessagesDialog::setCanSend(bool can_send) {
  send_btn_->setEnabled(can_send);
  send_btn_->setToolTip(can_send ? QString()
                                 : "需要真实呼号 + 已连接 APRS-IS 或 RF");
}

void AprsMessagesDialog::onSend() {
  const QString to = to_edit_->text().trimmed().toUpper();
  const QString text = text_edit_->text().trimmed();
  if (to.isEmpty() || text.isEmpty()) return;
  emit sendRequested(to, text);
  text_edit_->clear();
}

void AprsMessagesDialog::reload(const radio::aprs::MessageStore& store) {
  const auto& inbox = store.inbox();
  inbox_->setRowCount(static_cast<int>(inbox.size()));
  for (int i = 0; i < static_cast<int>(inbox.size()); ++i) {
    const auto& m = inbox[static_cast<size_t>(i)];
    auto* time = new QTableWidgetItem(when(m.at_ms));
    if (!m.read) {
      time->setBackground(QColor(70, 90, 40));
      QFont f = time->font();
      f.setBold(true);
      time->setFont(f);
    }
    inbox_->setItem(i, 0, time);
    inbox_->setItem(i, 1, new QTableWidgetItem(QString::fromStdString(m.from)));
    inbox_->setItem(i, 2, new QTableWidgetItem(QString::number(m.line)));
    inbox_->setItem(i, 3, new QTableWidgetItem(QString::fromStdString(m.text)));
  }

  const auto& outbox = store.outbox();
  outbox_->setRowCount(static_cast<int>(outbox.size()));
  for (int i = 0; i < static_cast<int>(outbox.size()); ++i) {
    const auto& m = outbox[static_cast<size_t>(i)];
    outbox_->setItem(i, 0, new QTableWidgetItem(when(m.created_ms)));
    outbox_->setItem(i, 1, new QTableWidgetItem(QString::fromStdString(m.to)));
    outbox_->setItem(i, 2, new QTableWidgetItem(QString::fromStdString(m.id)));
    outbox_->setItem(i, 3, new QTableWidgetItem(QString::number(m.tries)));
    const QString state = m.acked      ? "已确认"
                          : m.rejected ? "被拒绝"
                                       : m.tries > 0 ? "已发/待确认" : "排队";
    outbox_->setItem(i, 4, new QTableWidgetItem(state));
    outbox_->setItem(i, 5, new QTableWidgetItem(QString::fromStdString(m.text)));
  }

  summary_->setText(QString("未读 %1 / 收 %2 / 发 %3 · 本机 %4")
                        .arg(store.unread())
                        .arg(inbox.size())
                        .arg(outbox.size())
                        .arg(my_call_));
}

}  // namespace radio::app
