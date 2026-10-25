// SPDX-License-Identifier: AGPL-3.0-or-later
#include "app/aprs_panel.h"

#include <QLabel>
#include <QPushButton>
#include <QRegularExpression>
#include <QVBoxLayout>

namespace radio::app {

namespace {
// 国内优先，国际回落（连通性因网络环境而异，两跳都试）
struct AprsIsEntry { const char* host; quint16 port; };
constexpr AprsIsEntry kServers[] = {
    {"china.aprs2.net", 10152},
    {"rotate.aprs2.net", 14580},
};
}  // namespace

AprsPanel::AprsPanel(QWidget* parent) : QWidget(parent) {
  auto* root = new QVBoxLayout(this);
  auto* head = new QHBoxLayout();
  head->addWidget(new QLabel("APRS-IS 台流（接收侧；解析设计参考 APRSLocus，AGPL §13）", this));
  head->addStretch(1);
  auto* btn = new QPushButton("连接/断开", this);
  connect(btn, &QPushButton::clicked, this, &AprsPanel::toggleConnect);
  head->addWidget(btn);
  root->addLayout(head);

  view_ = new QListWidget(this);
  view_->setMaximumHeight(120);
  view_->setAlternatingRowColors(true);
  root->addWidget(view_);

  sock_ = new QTcpSocket(this);
  connect(sock_, &QTcpSocket::connected, this, &AprsPanel::onConnected);
  connect(sock_, &QTcpSocket::readyRead, this, &AprsPanel::onReadyRead);
  connect(sock_, &QTcpSocket::disconnected, this, [this] {
    // 国内入口失败 → 回落国际轮询
    if (!isConnected() && try_server_ + 1 < sizeof(kServers) / sizeof(kServers[0])) {
      ++try_server_;
      const auto& s = kServers[try_server_];
      view_->addItem(QString("回落 %1:%2 …").arg(s.host).arg(s.port));
      sock_->connectToHost(s.host, s.port);
      return;
    }
    emit statusMessage(QString("APRS-IS 断开（累计台站 %1）").arg(station_count_));
  });
}

AprsPanel::~AprsPanel() = default;

void AprsPanel::setLoginCall(const QString& call) {
  if (!call.isEmpty()) login_call_ = call;
}

void AprsPanel::toggleConnect() {
  if (isConnected()) {
    sock_->disconnectFromHost();
    return;
  }
  // 国内入口优先；连接失败自动回落国际轮询
  try_server_ = 0;
  const auto& s = kServers[try_server_];
  view_->addItem(QString("连接 %1:%2 …").arg(s.host).arg(s.port));
  sock_->connectToHost(s.host, s.port);
}

void AprsPanel::onConnected() {
  // login 行：pass -1 = 只收；filter m/50 周边台（登录后服务器按滤器推流）
  sock_->write(QString("user %1 pass -1 vers radio-suite 0.1 filter m/50\r\n")
                   .arg(login_call_)
                   .toUtf8());
  emit statusMessage(QString("APRS-IS 已连接 %1（%2，只收）")
                         .arg(kServers[try_server_].host)
                         .arg(login_call_));
}

void AprsPanel::onReadyRead() {
  rx_buf_ += QString::fromUtf8(sock_->readAll());
  int nl = 0;
  while ((nl = rx_buf_.indexOf('\n')) >= 0) {
    const QString line = rx_buf_.left(nl).trimmed();
    rx_buf_.remove(0, nl + 1);
    if (!line.isEmpty()) appendLine(line);
  }
}

void AprsPanel::appendLine(const QString& raw) {
  if (raw.startsWith('#')) return;  // 服务器注释/心跳
  const int gt = raw.indexOf('>');
  if (gt <= 0) return;
  const QString call = raw.left(gt);
  const QString info = raw.mid(gt + 1);

  // 位置报告（! 或 = 起始，未压缩格式 ddmm.mmN/dddmm.mmE）
  QString coord, comment;
  static const QRegularExpression re(
      "[!=]?(\\d{4}\\.\\d{2}[NS])/?(\\d{5}\\.\\d{2}[EW])");
  const auto m = re.match(info);
  if (m.hasMatch()) {
    coord = m.captured(1) + " " + m.captured(2);
    comment = info.mid(m.capturedEnd());
  } else {
    comment = info;
  }
  const QString shown = coord.isEmpty()
                            ? QString("%1  %2").arg(call, comment.left(40))
                            : QString("%1  %2  %3").arg(call, coord, comment.left(30));
  view_->addItem(shown);
  ++station_count_;
  while (view_->count() > kMaxRows) delete view_->item(0);
  view_->scrollToBottom();
}

}  // namespace radio::app
