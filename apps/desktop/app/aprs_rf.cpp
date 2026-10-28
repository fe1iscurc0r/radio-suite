// SPDX-License-Identifier: AGPL-3.0-or-later
#include "app/aprs_rf.h"

namespace radio::app {

KissLink::KissLink(QObject* parent) : QObject(parent) {
  tcp_ = new QTcpSocket(this);
  connect(tcp_, &QTcpSocket::readyRead, this,
          [this] { onBytes(tcp_->readAll()); });
  connect(tcp_, &QTcpSocket::connected, this, [this] {
    desc_ = QString("KISS TCP %1:%2")
                .arg(tcp_->peerName())
                .arg(tcp_->peerPort());
    emit statusChanged(desc_ + " 已连接");
  });
  connect(tcp_, &QTcpSocket::disconnected, this, [this] {
    emit statusChanged(desc_.isEmpty() ? "KISS 断开" : desc_ + " 断开");
  });
  connect(tcp_, &QTcpSocket::errorOccurred, this,
          [this](QAbstractSocket::SocketError) { fail(tcp_->errorString()); });
}

void KissLink::connectTcp(const QString& host, quint16 port) {
  disconnectLink();
  desc_ = QString("KISS TCP %1:%2").arg(host).arg(port);
  tcp_->connectToHost(host, port);
}

#ifdef RADIO_SUITE_HAVE_SERIALPORT
void KissLink::connectSerial(const QString& port_name, int baud,
                             int kiss_port) {
  disconnectLink();
  if (serial_ == nullptr) {
    serial_ = new QSerialPort(this);
    connect(serial_, &QSerialPort::readyRead, this,
            [this] { onBytes(serial_->readAll()); });
    connect(serial_, &QSerialPort::errorOccurred, this,
            [this](QSerialPort::SerialPortError e) {
              if (e == QSerialPort::NoError) return;
              fail(serial_->errorString());
            });
  }
  serial_->setPortName(port_name);
  serial_->setBaudRate(baud);
  serial_->setDataBits(QSerialPort::Data8);
  serial_->setParity(QSerialPort::NoParity);
  serial_->setStopBits(QSerialPort::OneStop);
  serial_->setFlowControl(QSerialPort::NoFlowControl);
  kiss_port_ = static_cast<uint8_t>(kiss_port < 0 ? 0 : kiss_port);
  desc_ = QString("KISS 串口 %1 @%2").arg(port_name).arg(baud);
  if (!serial_->open(QIODevice::ReadWrite)) {
    fail(serial_->errorString());
    return;
  }
  emit statusChanged(desc_ + " 已打开");
}
#endif

void KissLink::disconnectLink() {
  decoder_.reset();
  if (tcp_->state() != QAbstractSocket::UnconnectedState)
    tcp_->disconnectFromHost();
#ifdef RADIO_SUITE_HAVE_SERIALPORT
  if (serial_ != nullptr && serial_->isOpen()) serial_->close();
#endif
}

bool KissLink::isConnected() const {
  if (tcp_->state() == QAbstractSocket::ConnectedState) return true;
#ifdef RADIO_SUITE_HAVE_SERIALPORT
  if (serial_ != nullptr && serial_->isOpen()) return true;
#endif
  return false;
}

void KissLink::onBytes(const QByteArray& bytes) {
  const auto* data = reinterpret_cast<const uint8_t*>(bytes.constData());
  decoder_.feed(std::span<const uint8_t>(data, static_cast<size_t>(bytes.size())));
  radio::aprs::KissFrame frame;
  while (decoder_.pop(&frame)) {
    emit ax25Frame(QByteArray(reinterpret_cast<const char*>(frame.data.data()),
                              static_cast<int>(frame.data.size())));
  }
}

bool KissLink::sendAx25(std::span<const uint8_t> frame) {
  const auto kiss = radio::aprs::kissEncode(frame, kiss_port_);
  const QByteArray bytes(reinterpret_cast<const char*>(kiss.data()),
                         static_cast<int>(kiss.size()));
  if (tcp_->state() == QAbstractSocket::ConnectedState) {
    tcp_->write(bytes);
    return true;
  }
#ifdef RADIO_SUITE_HAVE_SERIALPORT
  if (serial_ != nullptr && serial_->isOpen()) {
    serial_->write(bytes);
    return true;
  }
#endif
  return false;
}

void KissLink::fail(const QString& why) {
  emit statusChanged((desc_.isEmpty() ? QString("KISS") : desc_) +
                     " 错误：" + why);
}

}  // namespace radio::app
