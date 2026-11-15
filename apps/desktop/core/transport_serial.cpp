// SPDX-License-Identifier: AGPL-3.0-or-later
#include "core/transport_serial.h"

#include <QSerialPort>

#include "core/civ.h"

namespace radio::core {

SerialCivTransport::SerialCivTransport(Config cfg) : cfg_(std::move(cfg)) {}

SerialCivTransport::~SerialCivTransport() { close(); }

bool SerialCivTransport::open(std::string* error) {
  if (port_) return true;
  if (cfg_.port.empty()) {
    if (error) *error = "串口名为空：给 --serial COM3 或 /dev/ttyUSB0 这样的设备名";
    return false;
  }
  rx_.clear();
  port_ = new QSerialPort;
  port_->setPortName(QString::fromStdString(cfg_.port));
  port_->setBaudRate(cfg_.baud);
  port_->setDataBits(QSerialPort::Data8);
  port_->setParity(QSerialPort::NoParity);
  port_->setStopBits(QSerialPort::OneStop);
  port_->setFlowControl(QSerialPort::NoFlowControl);
  if (!port_->open(QIODevice::ReadWrite)) {
    if (error)
      *error = QString("串口 %1 @%2 打不开：%3")
                   .arg(QString::fromStdString(cfg_.port))
                   .arg(cfg_.baud)
                   .arg(port_->errorString())
                   .toStdString();
    delete port_;
    port_ = nullptr;
    return false;
  }
  log_ += "opened " + cfg_.port + " @" + std::to_string(cfg_.baud) + " 8N1\n";
  return true;
}

void SerialCivTransport::close() {
  if (!port_) return;
  if (port_->isOpen()) port_->close();
  delete port_;
  port_ = nullptr;
  rx_.clear();
}

bool SerialCivTransport::isOpen() const { return port_ && port_->isOpen(); }

bool SerialCivTransport::send(std::span<const uint8_t> frame) {
  if (!isOpen() || frame.empty()) return false;
  const QByteArray data(reinterpret_cast<const char*>(frame.data()),
                        static_cast<qint64>(frame.size()));
  return port_->write(data) == data.size() && port_->flush();
}

size_t SerialCivTransport::poll() {
  if (!port_ || !port_->isOpen()) return 0;
  size_t frames = 0;
  while (port_->waitForReadyRead(0) || port_->bytesAvailable() > 0) {
    frames += drain();
    if (port_->bytesAvailable() <= 0) break;
  }
  return frames;
}

size_t SerialCivTransport::drain() {
  rx_.append(port_->readAll());
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
  if (rx_.size() > civ::kMaxPendingStreamBytes) {
    discarded_ += static_cast<size_t>(rx_.size());
    log_ += "drop " + std::to_string(rx_.size()) +
            " bytes without terminator (stream out of sync)\n";
    rx_.clear();
  }
  return frames;
}

}  // namespace radio::core