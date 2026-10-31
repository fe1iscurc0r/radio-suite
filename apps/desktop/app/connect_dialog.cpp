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

namespace app = radio::app;

ConnectDialog::ConnectDialog(app::LinkPrefs prefs, QWidget* parent)
    : QDialog(parent), prefs_(std::move(prefs)) {
  setWindowTitle("连接设置（Connect Setting）");
  setMinimumWidth(460);
  auto* root = new QVBoxLayout(this);

  const bool bridge = app::bridgeAvailable();

  auto* row1 = new QHBoxLayout();
  row1->addWidget(new QLabel("链路", this));
  mode_ = new QComboBox(this);
  mode_->addItem("mock（内置模拟电台）");
  mode_->addItem(bridge ? "C++ 原生 UDP（主线）"
                        : "C++ 原生 UDP（手机上唯一可用的真机链路）");
  if (bridge) mode_->addItem("rsba1-core Python 桥（后手兜底）");
  mode_->setCurrentIndex(app::modeIndexFromLink(prefs_.link, bridge));
  row1->addWidget(mode_, 1);
  root->addLayout(row1);

  auto* row2 = new QHBoxLayout();
  row2->addWidget(new QLabel("电台 IP", this));
  host_ = new QLineEdit(prefs_.host.empty() ? QStringLiteral("192.168.0.31")
                                            : QString::fromStdString(prefs_.host),
                        this);
  row2->addWidget(host_, 1);
  root->addLayout(row2);

  auto* row3 = new QHBoxLayout();
  row3->addWidget(new QLabel("用户名", this));
  user_ = new QLineEdit(QString::fromStdString(prefs_.user), this);
  row3->addWidget(user_, 1);
  root->addLayout(row3);

  auto* row4 = new QHBoxLayout();
  row4->addWidget(new QLabel("密码", this));
  pass_ = new QLineEdit(QString::fromStdString(prefs_.pass), this);
  pass_->setEchoMode(QLineEdit::Password);
  row4->addWidget(pass_, 1);
  root->addLayout(row4);

  if (bridge) {
    auto* row5 = new QHBoxLayout();
    row5->addWidget(new QLabel("Python", this));
    python_ = new QLineEdit(QString::fromStdString(prefs_.python_exe), this);
    python_->setPlaceholderText("留空 = 用 PATH 上的 python");
    row5->addWidget(python_, 1);
    root->addLayout(row5);
  }

  cmdline_ = new QLabel(this);
  cmdline_->setStyleSheet("color:#8fe; font-family:Consolas;");
  cmdline_->setWordWrap(true);
  root->addWidget(cmdline_);

  auto* hint = new QLabel(
      "保存即写入设置文件（电台口令明文存在用户设置目录），重启 radio-suite 生效。",
      this);
  hint->setWordWrap(true);
  hint->setStyleSheet("color:#8a93a5;");
  root->addWidget(hint);

  connect(mode_, &QComboBox::currentTextChanged, this,
          [this] { syncPreview(); });
  connect(host_, &QLineEdit::textChanged, this, [this] { syncPreview(); });
  connect(user_, &QLineEdit::textChanged, this, [this] { syncPreview(); });
  connect(pass_, &QLineEdit::textChanged, this, [this] { syncPreview(); });
  syncPreview();

  auto* rowBtn = new QHBoxLayout();
  rowBtn->addStretch(1);
#ifndef Q_OS_ANDROID
  // 桌面上仍可以只抄一行命令去脚本里跑，不动持久化偏好
  auto* copy = new QPushButton("复制命令行", this);
  connect(copy, &QPushButton::clicked, this, &ConnectDialog::onCopy);
  rowBtn->addWidget(copy);
#endif
  auto* save = new QPushButton("保存", this);
  connect(save, &QPushButton::clicked, this, &ConnectDialog::onSave);
  rowBtn->addWidget(save);
  auto* close = new QPushButton("关闭", this);
  close->setDefault(true);
  connect(close, &QPushButton::clicked, this, &QDialog::reject);
  rowBtn->addWidget(close);
  root->addLayout(rowBtn);
}

app::LinkPrefs ConnectDialog::collect() const {
  app::LinkPrefs p;
  p.link = static_cast<app::Link>(
      app::linkFromModeIndex(mode_->currentIndex(), app::bridgeAvailable()));
  // 表单是用户当面表态，优先级与命令行同级；把当前内容当作「cli」侧喂给
  // resolveLink，可以让同一套降级判定（缺 UDP / IP 为空 / 没有 python）生效
  p.explicit_cli = true;
  p.host = host_->text().toStdString();
  p.user = user_->text().toStdString();
  p.pass = pass_->text().toStdString();
  p.python_exe = python_ ? python_->text().toStdString() : std::string{};
  return p;
}

void ConnectDialog::syncPreview() {
  const app::LinkPrefs wanted = collect();
  const app::LinkPrefs eff = app::resolveLink(wanted, app::LinkPrefs{});
  QString text = QString("下次启动 → %1").arg(app::linkStatusText(eff));
  if (eff.link != wanted.link)
    text += QString("（所选链路在本机建不出来，实际落到 %1）")
                .arg(app::linkName(eff.link));
#ifndef Q_OS_ANDROID
  QString cmd = "radio-suite.exe";
  if (eff.link == app::Link::Udp)
    cmd += QString(" --radio %1 --user %2 --pass %3")
               .arg(QString::fromStdString(wanted.host),
                    QString::fromStdString(wanted.user),
                    QString::fromStdString(wanted.pass));
  else if (eff.link == app::Link::PyBridge)
    cmd += QString(" --radio %1 --user %2 --pass %3 --radio-py")
               .arg(QString::fromStdString(wanted.host),
                    QString::fromStdString(wanted.user),
                    QString::fromStdString(wanted.pass));
  else
    cmd += " --mock";
  text += "\n" + cmd;
#endif
  cmdline_->setText(text);
}

void ConnectDialog::onSave() {
  prefs_ = collect();
  accept();
}

void ConnectDialog::onCopy() {
  QGuiApplication::clipboard()->setText(cmdline_->text());
}
