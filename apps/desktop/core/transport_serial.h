// SPDX-License-Identifier: AGPL-3.0-or-later
// radio-suite · core/transport_serial.h — CI-V over 串口（电台 CAT 口直连）
//
// 这是 CivTcpGateway 缺的那一半：手机没有 QtSerialPort、也拿不到电台的 USB CAT 口，
// PC 上的网关却需要「电台侧是个 ICivTransport」才能把 TCP 那头的字节递过来。
// 落地前网关只有 --mock（tools/civ_tcp_gateway.cpp 的注释就是这么写明的），
// 所以 A3 的全部意义就是让「手机 → TCP → 网关 → 串口 → 真电台」这条路成立。
//
// 帧定界沿用 TCP 那套：按 FD(0xFD) 切。CI-V 应答数据里真出现 0xFD 会错切一刀，
// 这与串口/RS-BA1 字节管道是同一层风险，先不假装能解决；攒超过 kMaxPendingBytes
// 还没见到 FD 就判定流错位，丢掉重同步。
//
// 为什么要 ICivBytePipe 这层接缝：台架上没有 CAT 线，但粘包、半包、脏前缀、
// 超长无尾这些坑跟有没有硬件完全无关。把它们做成可注入的内存环回，CI 上就能真跑
// 一遍；真串口的后端只在套件带 Qt6::SerialPort 时出厂（见 transport_serial_qt.cpp）。
#pragma once

#include <QByteArray>

#include <cstdint>
#include <memory>
#include <span>
#include <string>

#include "core/transport.h"

namespace radio::core {

// 字节管道：open/close/isOpen + 裸字节读写。不做任何 CI-V 语义。
class ICivBytePipe {
 public:
  virtual ~ICivBytePipe() = default;
  virtual bool open(std::string* error) = 0;
  virtual void close() = 0;
  virtual bool isOpen() const = 0;
  virtual bool write(std::span<const uint8_t> bytes) = 0;
  // 此刻有多少读多少（没有就返回空），不阻塞。
  virtual QByteArray readAvailable() = 0;
};

// 真串口后端（8N1）。没编入 Qt6::SerialPort 的构建里返回 nullptr，
// 于是 SerialCivTransport::open() 会如实报「本构建没有串口后端」而不是静默失败。
std::unique_ptr<ICivBytePipe> makeSerialPipe(const std::string& port_name, int baud);

class SerialCivTransport final : public ICivTransport {
 public:
  struct Config {
    std::string port_name;   // Windows: "COM3"；Linux/macOS: "/dev/ttyUSB0"
    int baud = 9600;         // IC-705 出厂 CAT 速率是 9600；别默认 115200
    int write_timeout_ms = 200;
  };

  // 用真串口后端（套件缺 Qt6::SerialPort 时 open() 如实失败）。
  explicit SerialCivTransport(Config cfg);
  // 注入管道：用例走这条，台架无 CAT 线也能跑完整往返。
  SerialCivTransport(Config cfg, std::unique_ptr<ICivBytePipe> pipe);
  ~SerialCivTransport() override;

  bool open(std::string* error) override;
  void close() override;
  bool isOpen() const override;
  bool send(std::span<const uint8_t> frame) override;

  // 由 session 的 250 ms 轮询（或网关的 pump）驱动：读干净缓冲并按 FD 切帧。
  size_t poll() override;

  // 诊断用（台账页/状态栏/网关日志）
  const std::string& lastLog() const { return log_; }
  size_t framesDelivered() const { return delivered_; }
  size_t bytesDiscarded() const { return discarded_; }

 private:
  // 把 rx_ 里已成形的帧切出来上抛，返回本次投递条数。
  size_t parseBuffer();

  Config cfg_;
  std::unique_ptr<ICivBytePipe> pipe_;
  QByteArray rx_;
  std::string log_;
  size_t delivered_ = 0;
  size_t discarded_ = 0;
};

}  // namespace radio::core
