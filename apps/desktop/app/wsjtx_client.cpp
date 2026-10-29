// SPDX-License-Identifier: AGPL-3.0-or-later
#include "app/wsjtx_client.h"

#include <QByteArray>
#include <QNetworkDatagram>
#include <QUdpSocket>
#include <span>
#include <variant>

namespace radio::app {

WsjtxClient::WsjtxClient(QObject* parent) : QObject(parent) {
  qRegisterMetaType<radio::wsjtx::Status>("radio::wsjtx::Status");
  qRegisterMetaType<radio::wsjtx::Decode>("radio::wsjtx::Decode");
  qRegisterMetaType<radio::wsjtx::QsoLogged>("radio::wsjtx::QsoLogged");
  qRegisterMetaType<radio::wsjtx::WsprDecode>("radio::wsjtx::WsprDecode");
}

WsjtxClient::~WsjtxClient() { stop(); }

bool WsjtxClient::start(const QString& group, quint16 port, QString* error) {
  stop();
  group_ = group;
  port_ = port;
  socket_ = new QUdpSocket(this);
  if (!socket_->bind(QHostAddress::AnyIPv4, port_,
                     QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint)) {
    if (error)
      *error = QString("UDP 绑定失败（%1:%2）：%3")
                   .arg(group_)
                   .arg(port_)
                   .arg(socket_->errorString());
    socket_->deleteLater();
    socket_ = nullptr;
    return false;
  }
  port_ = socket_->localPort();
  const QHostAddress address(group_);
  if (address.isNull()) {
    if (error) *error = QString("无效的组播/单播地址：%1").arg(group_);
    stop();
    return false;
  }
  if (address.isMulticast() && !socket_->joinMulticastGroup(address)) {
    // 多网卡/VPN 机器上系统常返回 "Unknown error"，补一句可执行建议。
    if (error)
      *error = QString("加入组播失败（%1）：%2（可在设置里改用单播地址，"
                       "如本机 127.0.0.1）")
                   .arg(group_)
                   .arg(socket_->errorString());
    stop();
    return false;
  }
  connect(socket_, &QUdpSocket::readyRead, this, &WsjtxClient::onReadyRead);
  emit listeningChanged(true);
  return true;
}

void WsjtxClient::stop() {
  if (!socket_) return;
  socket_->disconnect(this);
  const QHostAddress address(group_);
  if (address.isMulticast()) socket_->leaveMulticastGroup(address);
  socket_->close();
  socket_->deleteLater();
  socket_ = nullptr;
  emit listeningChanged(false);
}

bool WsjtxClient::isListening() const { return socket_ != nullptr; }

void WsjtxClient::setClientId(const QString& id) {
  if (!id.trimmed().isEmpty()) client_id_ = id.trimmed();
}

bool WsjtxClient::send(const radio::wsjtx::Message& message) {
  if (!socket_) return false;
  const auto wire = radio::wsjtx::encode(message);
  const QByteArray datagram(reinterpret_cast<const char*>(wire.data()),
                            static_cast<int>(wire.size()));
  return socket_->writeDatagram(datagram, QHostAddress(group_), port_) ==
         static_cast<qint64>(datagram.size());
}

bool WsjtxClient::sendReply(const radio::wsjtx::Decode& decode, quint8 modifiers) {
  return send(radio::wsjtx::makeReply(client_id_.toStdString(), decode, modifiers));
}

bool WsjtxClient::haltTx(bool auto_tx_only) {
  return send(radio::wsjtx::makeHaltTx(client_id_.toStdString(), auto_tx_only));
}

bool WsjtxClient::freeText(const QString& text, bool send_now) {
  return send(radio::wsjtx::makeFreeText(client_id_.toStdString(),
                                         text.toStdString(), send_now));
}

bool WsjtxClient::replay() {
  return send(radio::wsjtx::makeReplay(client_id_.toStdString()));
}

bool WsjtxClient::setLocation(const QString& grid) {
  return send(radio::wsjtx::makeLocation(client_id_.toStdString(),
                                         grid.toStdString()));
}

bool WsjtxClient::clearDecodes(int window) {
  return send(radio::wsjtx::makeClear(client_id_.toStdString(),
                                      static_cast<uint8_t>(window & 0xFF)));
}

void WsjtxClient::onReadyRead() {
  if (!socket_) return;
  while (socket_->hasPendingDatagrams()) {
    const QNetworkDatagram datagram = socket_->receiveDatagram();
    const QByteArray data = datagram.data();
    if (data.isEmpty()) continue;
    const auto bytes = std::span<const uint8_t>(
        reinterpret_cast<const uint8_t*>(data.constData()),
        static_cast<size_t>(data.size()));
    radio::wsjtx::Message message;
    std::string err;
    if (!radio::wsjtx::decode(bytes, &message, &err)) {
      emit error(QString("WSJT-X 报文被丢弃：%1").arg(QString::fromStdString(err)));
      continue;
    }
    const QString id = QString::fromStdString(message.id);
    if (const auto* status = std::get_if<radio::wsjtx::Status>(&message.body))
      emit statusReceived(*status, id);
    else if (const auto* decode = std::get_if<radio::wsjtx::Decode>(&message.body))
      emit decodeReceived(*decode, id);
    else if (const auto* qso = std::get_if<radio::wsjtx::QsoLogged>(&message.body))
      emit qsoLogged(*qso, id);
    else if (const auto* wspr = std::get_if<radio::wsjtx::WsprDecode>(&message.body))
      emit wsprDecode(*wspr, id);
    else if (const auto* adif = std::get_if<radio::wsjtx::LoggedAdif>(&message.body))
      emit loggedAdif(QString::fromStdString(adif->adif), id);
    else if (const auto* hb = std::get_if<radio::wsjtx::Heartbeat>(&message.body))
      emit heartbeatReceived(id, QString::fromStdString(hb->version),
                             QString::fromStdString(hb->revision));
    else if (std::holds_alternative<radio::wsjtx::Close>(message.body))
      emit clientClosed(id);
  }
}

}  // namespace radio::app
