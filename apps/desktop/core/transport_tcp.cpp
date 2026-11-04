// SPDX-License-Identifier: AGPL-3.0-or-later
#include "core/transport_tcp.h"

#include <QTcpSocket>

#include <cstring>

#include "core/civ.h"

namespace radio::core {
namespace {

// 一帧 CI-V 最长也就几十字节；攒到这个数还没见到 FD，说明流已经错位
//（对端不是 CI-V，或者中间被截断），丢掉重同步比继续攒着诚实。
constexpr int kMaxPendingBytes = 4096;

}  // namespace

TcpCivTransport::TcpCivTransport(Config cfg) : cfg_(std::move(cfg)) {}

TcpCivTransport::~TcpCivTransport() { close(); }

bool TcpCivTransport::open(std::string* error) {
  if (sock_) return true;
  rx_.clear();
  sock_ = new QTcpSocket;
  sock_->connectToHost(QString::fromStdString(cfg_.host), cfg_.port);
  if (!sock_->waitForConnected(cfg_.connect_timeout_ms)) {
    if (error)
      *error = QString("TCP 连接 %1:%2 失败：%3")
                   .arg(QString::fromStdString(cfg_.host))
                   .arg(cfg_.port)
                   .arg(sock_->errorString())
                   .toStdString();
    delete sock_;
    sock_ = nullptr;
    return false;
  }
  log_ += "connected " + cfg_.host + ":" + std::to_string(cfg_.port) + "\n";
  return true;
}

void TcpCivTransport::close() {
  if (!sock_) return;
  sock_->disconnectFromHost();
  delete sock_;  // 不等对端回话：析构路径上再等一次超时不值
  sock_ = nullptr;
  rx_.clear();
}

bool TcpCivTransport::isOpen() const {
  return sock_ && sock_->state() == QAbstractSocket::ConnectedState;
}

bool TcpCivTransport::send(std::span<const uint8_t> frame) {
  if (!isOpen() || frame.empty()) return false;
  const QByteArray data(reinterpret_cast<const char*>(frame.data()),
                        static_cast<qint64>(frame.size()));
  return sock_->write(data) == data.size() && sock_->flush();
}

size_t TcpCivTransport::poll() {
  if (!sock_) return 0;
  if (!isOpen()) {
    // 对端断开要如实反映：QTcpSocket 的 disconnected 信号得靠事件循环，
    // 而 session 是 250 ms 轮询驱动的，这里主动记一笔并交给上层看 isOpen()。
    static_cast<void>(sock_->waitForDisconnected(0));
    return 0;
  }
  size_t frames = 0;
  while (sock_->waitForReadyRead(0) || sock_->bytesAvailable() > 0) {
    frames += drain();
    if (sock_->bytesAvailable() <= 0) break;
  }
  return frames;
}

size_t TcpCivTransport::drain() {
  rx_.append(sock_->readAll());
  const std::span<const uint8_t> buf(
      reinterpret_cast<const uint8_t*>(rx_.constData()),
      static_cast<size_t>(rx_.size()));
  size_t frames = 0;
  const size_t consumed = civ::extractFrames(
      buf,
      [this, &frames](std::span<const uint8_t> f) {
        emitFrame(f);
        ++frames;
        ++delivered_;
      },
      &discarded_);
  rx_.remove(0, static_cast<qint64>(consumed));
  if (rx_.size() > kMaxPendingBytes) {
    discarded_ += static_cast<size_t>(rx_.size());
    log_ += "drop " + std::to_string(rx_.size()) +
            " bytes without terminator (stream out of sync)\n";
    rx_.clear();
  }
  return frames;
}

}  // namespace radio::core
