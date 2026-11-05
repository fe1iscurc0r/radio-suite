// SPDX-License-Identifier: AGPL-3.0-or-later
// radio-suite · core/transport_serial_qt.cpp — 真串口后端（Qt6::SerialPort）
//
// 这个 TU 只在套件带 Qt6::SerialPort 时参与编译（见 core/CMakeLists.txt）。
// 拆成单独一个文件是有意的：SerialCivTransport 的帧定界/重同步逻辑必须能在
// 没有 SerialPort 的机器（CI 的 Ubuntu 就没装）上照样编译、照样跑用例，
// 所以那些代码不许沾 QSerialPort。
#include "core/transport_serial.h"

#include <QSerialPort>

namespace radio::core {
namespace {

class QtSerialPipe final : public ICivBytePipe {
 public:
  QtSerialPipe(std::string port_name, int baud)
      : port_name_(std::move(port_name)), baud_(baud) {}

  ~QtSerialPipe() override { close(); }

  bool open(std::string* error) override {
    if (port_ && port_->isOpen()) return true;
    if (!port_) port_ = new QSerialPort;
    port_->setPortName(QString::fromStdString(port_name_));
    port_->setBaudRate(baud_);
    // CI-V 的 CAT 口一律 8N1，没有可选项
    port_->setDataBits(QSerialPort::Data8);
    port_->setParity(QSerialPort::NoParity);
    port_->setStopBits(QSerialPort::OneStop);
    port_->setFlowControl(QSerialPort::NoFlowControl);
    if (!port_->open(QIODevice::ReadWrite)) {
      if (error)
        *error = "串口 " + port_name_ + " 打不开：" +
                 port_->errorString().toStdString();
      delete port_;
      port_ = nullptr;
      return false;
    }
    // 上一轮残留的字节会被当成这一轮的帧头，开线前先丢掉
    port_->clear();
    return true;
  }

  void close() override {
    if (!port_) return;
    port_->close();
    delete port_;
    port_ = nullptr;
  }

  bool isOpen() const override { return port_ && port_->isOpen(); }

  bool write(std::span<const uint8_t> bytes) override {
    if (!isOpen()) return false;
    const QByteArray data(reinterpret_cast<const char*>(bytes.data()),
                          static_cast<qint64>(bytes.size()));
    if (port_->write(data) != data.size()) return false;
    return port_->waitForBytesWritten(kWriteTimeoutMs);
  }

  QByteArray readAvailable() override {
    if (!isOpen()) return {};
    return port_->readAll();
  }

 private:
  // 串口是同步口，写不动就等一小会儿；真电台的 CAT 口在这个量级不会拖过它。
  static constexpr int kWriteTimeoutMs = 200;

  std::string port_name_;
  int baud_ = 9600;
  QSerialPort* port_ = nullptr;
};

}  // namespace

std::unique_ptr<ICivBytePipe> makeSerialPipe(const std::string& port_name, int baud) {
  if (port_name.empty()) return nullptr;
  return std::make_unique<QtSerialPipe>(port_name, baud);
}

}  // namespace radio::core
