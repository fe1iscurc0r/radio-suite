// SPDX-License-Identifier: AGPL-3.0-or-later
#include "app/connect_dialog.h"

#include <QClipboard>
#include <QComboBox>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

ConnectDialog::ConnectDialog(QWidget* parent) : QDialog(parent) {
  setWindowTitle("连接设置（Connect Setting）");
  setMinimumWidth(460);
  auto* root = new QVBoxLayout(this);

  auto* row1 = new QHBoxLayout();
  row1->addWidget(new QLabel("链路", this));
  mode_ = new QComboBox(this);
  mode_->addItem("mock（内置模拟电台）");
  mode_->addItem("C++ 原生 UDP（主线）");
  mode_->addItem("rsba1-core Python 桥（后手兜底）");
  row1->addWidget(mode_, 1);
  root->addLayout(row1);

  auto* row2 = new QHBoxLayout();
  row2->addWidget(new QLabel("电台 IP", this));
  host_ = new QLineEdit("192.168.0.31", this);
  row2->addWidget(host_, 1);
  root->addLayout(row2);

  auto* row3 = new QHBoxLayout();
  row3->addWidget(new QLabel("用户名", this));
  user_ = new QLineEdit("linnan", this);
  row3->addWidget(user_, 1);
  root->addLayout(row3);

  auto* row4 = new QHBoxLayout();
  row4->addWidget(new QLabel("密码", this));
  pass_ = new QLineEdit(this);
  pass_->setEchoMode(QLineEdit::Password);
  row4->addWidget(pass_, 1);
  root->addLayout(row4);

  cmdline_ = new QLabel(this);
  cmdline_->setStyleSheet("color:#8fe; font-family:Consolas;");
  cmdline_->setWordWrap(true);
  root->addWidget(cmdline_);

  auto refresh = [this] {
    QString cmd = "radio-suite.exe";
    if (mode_->currentIndex() == 1)
      cmd += QString(" --radio %1 --user %2 --pass %3")
                 .arg(host_->text(), user_->text(), pass_->text());
    else if (mode_->currentIndex() == 2)
      cmd += QString(" --radio %1 --user %2 --pass %3 --radio-py")
                 .arg(host_->text(), user_->text(), pass_->text());
    else
      cmd += " --mock";
    cmdline_->setText(cmd);
  };
  connect(mode_, &QComboBox::currentTextChanged, refresh);
  connect(host_, &QLineEdit::textChanged, refresh);
  connect(user_, &QLineEdit::textChanged, refresh);
  connect(pass_, &QLineEdit::textChanged, refresh);
  refresh();

  auto* rowBtn = new QHBoxLayout();
  rowBtn->addStretch(1);
  auto* copy = new QPushButton("复制命令行", this);
  connect(copy, &QPushButton::clicked, this, &ConnectDialog::onCopy);
  rowBtn->addWidget(copy);
  auto* close = new QPushButton("关闭", this);
  connect(close, &QPushButton::clicked, this, &QDialog::accept);
  rowBtn->addWidget(close);
  root->addLayout(rowBtn);
}

void ConnectDialog::onCopy() {
  QGuiApplication::clipboard()->setText(cmdline_->text());
  cmdline_->setText(cmdline_->text() + "   ✓ 已复制（重启 radio-suite 生效）");
}
