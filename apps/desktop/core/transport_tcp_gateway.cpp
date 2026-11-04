// SPDX-License-Identifier: AGPL-3.0-or-later
#include "core/transport_tcp_gateway.h"

#include <QTcpServer>
#include <QTcpSocket>

#include "core/civ.h"

namespace radio::core {

CivTcpGateway::CivTcpGateway(Config cfg, std::shared_ptr<ICivTransport> rig)
    : cfg_(std::move(cfg)), rig_(std::move(rig)) {}

CivTcpGateway::~CivTcpGateway() { stop(); }

bool CivTcpGateway::start(std::string* error) {
  if (server_) return true;
  if (!rig_) {
    if (error) *error = "网关没有电台侧传输";
    return false;
  }
  std::string rig_err;
  if (!rig_->open(&rig_err)) {
    if (error) *error = "电台侧打不开：" + rig_err;
    return false;
  }
  rig_->setFrameCallback(
      [this](std::span<const uint8_t> f) { onRigFrame(f); });

  server_ = new QTcpServer;
  const QHostAddress addr(QString::fromStdString(cfg_.bind_addr));
  // QHostAddress 没有 isValid()：解析失败是 isNull()（Any/LocalHost 这类
  // 特殊地址另算，但这里只接受点分地址，填错就退回本机回环）
  if (!server_->listen(addr.isNull() ? QHostAddress::LocalHost : addr,
                       cfg_.port)) {
    if (error)
      *error = "监听 " + cfg_.bind_addr + ":" + std::to_string(cfg_.port) +
               " 失败：" + server_->errorString().toStdString();
    delete server_;
    server_ = nullptr;
    rig_->close();
    return false;
  }
  log_ += "listening " + cfg_.bind_addr + ":" + std::to_string(server_->serverPort()) +
          "\n";
  return true;
}

void CivTcpGateway::stop() {
  if (client_) {
    client_->disconnectFromHost();
    delete client_;
    client_ = nullptr;
  }
  if (server_) {
    server_->close();
    delete server_;
    server_ = nullptr;
  }
  if (rig_) rig_->close();
}

bool CivTcpGateway::listening() const {
  return server_ && server_->isListening();
}

uint16_t CivTcpGateway::port() const {
  return server_ ? static_cast<uint16_t>(server_->serverPort()) : 0;
}

void CivTcpGateway::acceptPending() {
  while (server_ && server_->hasPendingConnections()) {
    QTcpSocket* pending = server_->nextPendingConnection();
    if (client_) {
      // 一次只服务一根串口线：并排两个客户端会让两个操作者互相踩电台状态，
      // 与其默默排队，不如当面拒掉并留在日志里。
      log_ += "reject second client\n";
      pending->disconnectFromHost();
      pending->deleteLater();
      continue;
    }
    client_ = pending;
    log_ += "client " + client_->peerAddress().toString().toStdString() + ":" +
            std::to_string(client_->peerPort()) + " connected\n";
  }
}

void CivTcpGateway::onRigFrame(std::span<const uint8_t> frame) {
  if (!client_ || client_->state() != QAbstractSocket::ConnectedState) return;
  client_->write(reinterpret_cast<const char*>(frame.data()),
                 static_cast<qint64>(frame.size()));
}

size_t CivTcpGateway::pump() {
  acceptPending();
  if (!client_) return 0;
  if (client_->state() != QAbstractSocket::ConnectedState) {
    log_ += "client gone\n";
    client_->deleteLater();
    client_ = nullptr;
    return 0;
  }
  const QByteArray inbound = client_->readAll();
  if (!inbound.isEmpty()) {
    // ICivTransport::send() 的契约是「一次一条完整帧」，而 TCP 会把 session 一次
    // 撒出去的回读粘成几包。原样转发会让电台侧只认得第一帧，后面的默默丢掉。
    rx_.append(inbound);
    const std::span<const uint8_t> buf(
        reinterpret_cast<const uint8_t*>(rx_.constData()),
        static_cast<size_t>(rx_.size()));
    size_t writes = 0;
    const size_t consumed = civ::extractFrames(
        buf, [this, &writes](std::span<const uint8_t> frame) {
          ++writes;
          if (!rig_->send(frame)) log_ += "rig write failed\n";
        });
    rx_.remove(0, static_cast<qint64>(consumed));
    static_cast<void>(writes);  // 每 20 ms 一条会把 log 撑爆，帧数留给以后做统计
  }
  return rig_->poll();
}

}  // namespace radio::core
