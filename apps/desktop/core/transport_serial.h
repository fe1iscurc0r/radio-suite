// SPDX-License-Identifier: AGPL-3.0-or-later
// radio-suite · core/transport_serial.h — CI-V 串口对端（电台那一侧）
//
// 位置：这是**网关的电台侧**，不是手机侧。手机没有 QtSerialPort，也拿不到电台的
// USB CAT 口，所以 PC 上跑 civ_tcp_gateway：一头串口接电台，一头 TCP 接手机。
// 桌面直连电台时也可以直接用它当 ICivTransport。
//
// 定界与失步保护都复用 TCP 那条规则（civ::extractFrames + kMaxPendingStreamBytes），
// 不在这里写第二套。同样的已知风险：应答数据里真出现 0xFD 会被错切一刀。
//
// ⚠️ 未验（本机没有 CAT 线）：经典 CI-V 是单线开集总线，发送端会听见自己的回声；
// Icom 的 USB 虚拟串口是否回声没有实测过。回声一旦被切成帧上抛，0x1A 0x05（菜单）
// 与 0x14（电平）这类**设置帧与应答帧同形**的命令会被 session 当成回读，
// 也就是「写进去的值被自己的回声确认了」。这里不加没有实测依据的回声过滤
// （按地址过滤会顺手丢掉 to=0x00 的广播帧），只把 framesDelivered/bytesDiscarded
// 暴露出来，真机第一次接上时先看这两个数对不对。
#pragma once

#include <QByteArray>

#include <cstdint>
#include <string>

#include "core/transport.h"

class QSerialPort;

namespace radio::core {

class SerialCivTransport final : public ICivTransport {
 public:
  struct Config {
    std::string port;          // "COM3" / "/dev/ttyUSB0" / "/dev/cu.usbserial-xxxx"
    int baud = 19200;          // 必须与电台 CI-V 菜单里的波特率一致，不是随便挑的
  };

  explicit SerialCivTransport(Config cfg);
  ~SerialCivTransport() override;

  bool open(std::string* error) override;
  void close() override;
  bool isOpen() const override;
  bool send(std::span<const uint8_t> frame) override;

  // 由 session 的 250 ms 轮询 / 网关的 pump() 驱动：读干净接收缓冲，按 FD 切帧上抛。
  size_t poll() override;

  // 诊断用（台账页/状态栏）
  const std::string& lastLog() const { return log_; }
  size_t framesDelivered() const { return delivered_; }
  size_t bytesDiscarded() const { return discarded_; }

 private:
  size_t drain();

  Config cfg_;
  QSerialPort* port_ = nullptr;
  QByteArray rx_;
  std::string log_;
  size_t delivered_ = 0;
  size_t discarded_ = 0;
};

}  // namespace radio::core