// SPDX-License-Identifier: AGPL-3.0-or-later
#include "app/aprs_panel.h"

#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QPushButton>
#include <QRegularExpression>
#include <QTableWidget>
#include <QVBoxLayout>

#include "core/civ.h"

namespace radio::app {

namespace {
struct AprsIsEntry { const char* host; quint16 port; };
constexpr AprsIsEntry kServers[] = {
    {"china.aprs2.net", 10152},
    {"rotate.aprs2.net", 14580},
};
constexpr int kMaxStations = 300;
}  // namespace

AprsPanel::AprsPanel(QWidget* parent) : QWidget(parent) {
  auto* root = new QVBoxLayout(this);
  root->setContentsMargins(4, 2, 4, 2);

  auto* head = new QHBoxLayout();
  counter_ = new QLabel("APRS-IS（未连接）", this);
  head->addWidget(counter_, 1);
  beacon_btn_ = new QPushButton("发信标", this);
  connect(beacon_btn_, &QPushButton::clicked, this, &AprsPanel::sendBeacon);
  head->addWidget(beacon_btn_);
  msg_btn_ = new QPushButton("发消息", this);
  connect(msg_btn_, &QPushButton::clicked, this, &AprsPanel::sendMessage);
  head->addWidget(msg_btn_);
  auto* btn = new QPushButton("连接/断开", this);
  connect(btn, &QPushButton::clicked, this, &AprsPanel::toggleConnect);
  head->addWidget(btn);
  root->addLayout(head);

  // 台站聚合表：呼号 / 最近坐标 / 包数 / 最后收到（不再瀑布流刷屏）
  table_ = new QTableWidget(0, 4, this);
  table_->setHorizontalHeaderLabels({"呼号", "最近坐标", "包数", "最后收到"});
  table_->horizontalHeader()->setStretchLastSection(true);
  table_->setColumnWidth(0, 110);
  table_->setColumnWidth(1, 200);
  table_->setColumnWidth(2, 60);
  table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  table_->setMaximumHeight(150);
  table_->setSortingEnabled(true);
  root->addWidget(table_);

  sock_ = new QTcpSocket(this);
  connect(sock_, &QTcpSocket::connected, this, &AprsPanel::onConnected);
  connect(sock_, &QTcpSocket::readyRead, this, &AprsPanel::onReadyRead);
  connect(sock_, &QTcpSocket::disconnected, this, [this] {
    if (!isConnected() && try_server_ + 1 < sizeof(kServers) / sizeof(kServers[0])) {
      ++try_server_;
      const auto& s = kServers[try_server_];
      counter_->setText(QString("回落 %1:%2 …").arg(s.host).arg(s.port));
      sock_->connectToHost(s.host, s.port);
      return;
    }
    counter_->setText(QString("APRS-IS 断开（台站 %1 / 包 %2）")
                          .arg(stations_.size()).arg(total_packets_));
  });
}

AprsPanel::~AprsPanel() = default;

void AprsPanel::setLoginCall(const QString& call) {
  if (!call.isEmpty()) login_call_ = call;
  // N0CALL = 无真实呼号 → 禁用上行（合法业余操作须本人呼号）
  const bool can_tx = login_call_ != "N0CALL";
  beacon_btn_->setEnabled(can_tx);
  msg_btn_->setEnabled(can_tx);
  beacon_btn_->setToolTip(can_tx ? "位置信标上行（APRS-IS）"
                                 : "需真实呼号（HamLog settings 的 my_callsign）");
}

void AprsPanel::toggleConnect() {
  if (isConnected()) {
    sock_->disconnectFromHost();
    return;
  }
  try_server_ = 0;
  const auto& s = kServers[try_server_];
  counter_->setText(QString("连接 %1:%2 …").arg(s.host).arg(s.port));
  sock_->connectToHost(s.host, s.port);
}

void AprsPanel::onConnected() {
  // 有真实呼号带通行码（可上行），否则 pass -1 只收
  if (login_call_ != "N0CALL") {
    const auto pc = radio::core::civ::aprsPasscode(login_call_.toStdString());
    sock_->write(QString("user %1 pass %2 vers radio-suite 0.1 filter m/100 r/33/\r\n")
                     .arg(login_call_).arg(pc)
                     .toUtf8());
    counter_->setText(QString("APRS-IS 已连接 %1:%2（%3，可上行）")
                          .arg(kServers[try_server_].host)
                          .arg(kServers[try_server_].port)
                          .arg(login_call_));
  } else {
    sock_->write(QString("user %1 pass -1 vers radio-suite 0.1 filter m/100 r/33/\r\n")
                     .arg(login_call_)
                     .toUtf8());
    counter_->setText(QString("APRS-IS 已连接 %1:%2（只收——设真实呼号可上行）")
                          .arg(kServers[try_server_].host)
                          .arg(kServers[try_server_].port));
  }
  emit statusMessage("APRS-IS 已连接");
}

void AprsPanel::sendRaw(const QString& line) {
  if (!isConnected()) {
    emit statusMessage("APRS-IS 未连接，无法上行");
    return;
  }
  sock_->write(line.toUtf8());
  sock_->write("\r\n");
}

void AprsPanel::sendBeacon() {
  // 位置信标（无 GPS：坐标来自 HamLog settings my_qth 或 0/0 占位由用户改）
  const QString lat = "3114.32N", lon = "12121.92E";  // 上海占位，M1 接 my_qth
  const QString line = QString("%1>APRS,TCPIP*:!%2%3#radio-suite AGPL %4")
                           .arg(login_call_, lat, lon)
                           .arg(QDateTime::currentDateTimeUtc().toString("hhmm"));
  sendRaw(line);
  emit statusMessage("信标已上行（" + lat + " " + lon + "）");
}

void AprsPanel::sendMessage() {
  bool ok = false;
  const QString to = QInputDialog::getText(this, "APRS 消息", "目标呼号：",
                                           QLineEdit::Normal, {}, &ok);
  if (!ok || to.trimmed().isEmpty()) return;
  const QString text = QInputDialog::getText(this, "APRS 消息",
                                             QString("发给 %1 的文本：").arg(to),
                                             QLineEdit::Normal, {}, &ok);
  if (!ok || text.isEmpty()) return;
  // ADDRESSEE 固定 9 字符左对齐（APRS 规格）
  const QString addr = to.trimmed().toUpper().leftJustified(9, ' ');
  sendRaw(QString("%1>APRS,TCPIP*::%2:%3").arg(login_call_, addr, text));
  emit statusMessage(QString("消息已上行 → %1：%2").arg(to, text));
}

void AprsPanel::onReadyRead() {
  rx_buf_ += QString::fromUtf8(sock_->readAll());
  int nl = 0;
  while ((nl = rx_buf_.indexOf('\n')) >= 0) {
    const QString line = rx_buf_.left(nl).trimmed();
    rx_buf_.remove(0, nl + 1);
    if (!line.isEmpty()) handleLine(line);
  }
}

void AprsPanel::handleLine(const QString& raw) {
  if (raw.startsWith('#')) return;  // 服务器注释/心跳
  const int gt = raw.indexOf('>');
  if (gt <= 0) return;
  const QString call = raw.left(gt).section('-', 0, 0);
  const QString info = raw.mid(gt + 1);

  static const QRegularExpression re(
      "(\\d{4}\\.\\d{2}[NS])/?(\\d{5}\\.\\d{2}[EW])");
  const auto m = re.match(info);

  auto& st = stations_[call];  // 聚合：同台更新而非追加
  st.packets += 1;
  st.last_seen = QDateTime::currentDateTime();
  if (m.hasMatch()) {
    st.last_coord = m.captured(1) + " " + m.captured(2);
    st.comment = info.mid(m.capturedEnd()).left(24);
  } else {
    st.comment = info.left(24);
  }
  total_packets_ += 1;
  // 表刷新节流（200ms），不再每包刷屏
  static qint64 last_refresh = 0;
  const qint64 now = QDateTime::currentMSecsSinceEpoch();
  if (now - last_refresh > 200) {
    last_refresh = now;
    refreshTable();
  }
}

void AprsPanel::refreshTable() {
  table_->setSortingEnabled(false);
  table_->setRowCount(static_cast<int>(stations_.size()));
  int r = 0;
  for (auto it = stations_.begin(); it != stations_.end(); ++it, ++r) {
    table_->setItem(r, 0, new QTableWidgetItem(it.key()));
    table_->setItem(r, 1, new QTableWidgetItem(it.value().last_coord));
    auto* pk = new QTableWidgetItem(QString::number(it.value().packets));
    pk->setTextAlignment(Qt::AlignCenter);
    table_->setItem(r, 2, pk);
    table_->setItem(r, 3, new QTableWidgetItem(
                               it.value().last_seen.time().toString("hh:mm:ss")));
  }
  table_->setSortingEnabled(true);
  counter_->setText(QString("APRS-IS 台站 %1 / 包 %2")
                        .arg(stations_.size()).arg(total_packets_));
  while (stations_.size() > static_cast<size_t>(kMaxStations)) {
    // 超限淘汰最旧
    QString oldest;
    qint64 best = INT64_MAX;
    for (auto it = stations_.begin(); it != stations_.end(); ++it)
      if (it.value().last_seen.toMSecsSinceEpoch() < best) {
        best = it.value().last_seen.toMSecsSinceEpoch();
        oldest = it.key();
      }
    stations_.remove(oldest);
  }
}

}  // namespace radio::app
