// SPDX-License-Identifier: AGPL-3.0-or-later
// radio-suite · core/transport_tcp.h — CI-V over TCP（裸字节管道）
//
// 面向的是「一条 TCP 连接 = 一根串口线」的网关：PC/树莓派上的 ser2net、eth2rig
// 一类把电台 CAT 口透传成 TCP 的实现。刻意**不**做 rigctld 协议翻译——rigctld 是
// 文本命令协议，套不进 ICivTransport 的字节语义，而且会把 CI-V 专有命令（键控、
// 记忆信道、深设置树）全丢掉。
//
// 定界：按 FD(0xFD) 切帧。应答数据里真出现 0xFD 时会错切一刀——这与 RS-BA1
// 串口信道的字节管道语义是同一层风险，先按 FD 定界；真机若踩到，再改成超时定界。
#pragma once

#include <QByteArray>

#include <cstdint>
#include <string>

#include "core/transport.h"

class QTcpSocket;

namespace radio::core {

class TcpCivTransport final : public ICivTransport {
 public:
  struct Config {
    std::string host;
    uint16_t port = 4533;
    int connect_timeout_ms = 3000;
  };

  explicit TcpCivTransport(Config cfg);
  ~TcpCivTransport() override;

  bool open(std::string* error) override;
  void close() override;
  bool isOpen() const override;
  bool send(std::span<const uint8_t> frame) override;

  // 由 session 的 250 ms 轮询驱动：读干净接收缓冲，按 FD 切帧后逐条上抛。
  size_t poll() override;

  // 诊断用（台账页/状态栏）
  const std::string& lastLog() const { return log_; }
  size_t framesDelivered() const { return delivered_; }
  size_t bytesDiscarded() const { return discarded_; }

 private:
  size_t drain();

  Config cfg_;
  QTcpSocket* sock_ = nullptr;
  QByteArray rx_;
  std::string log_;
  size_t delivered_ = 0;
  size_t discarded_ = 0;
};

}  // namespace radio::core
