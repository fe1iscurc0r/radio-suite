// SPDX-License-Identifier: AGPL-3.0-or-later
// radio-suite · core/transport_tcp_gateway.h — CI-V over TCP 网关（PC 侧）
//
// 手机端没有 QtSerialPort，也拿不到电台的 USB CAT 口；但手机能连 TCP。
// 所以在 PC 上跑这一小块：一头是电台（串口或内置 mock），一头是一条 TCP 连接，
// 字节双向直传，不做任何协议翻译——「一条 TCP 连接 = 一根串口线」。
//
// 不用 QObject：radio_core 没开 AUTOMOC，而且这套东西本来就是被上层定时驱动的
// （session 用 250 ms 轮询，工具用一个 QTimer 调 pump()）。
#pragma once

#include <QByteArray>

#include <memory>
#include <string>

#include "core/transport.h"

class QTcpServer;
class QTcpSocket;

namespace radio::core {

class CivTcpGateway {
 public:
  struct Config {
    std::string bind_addr = "127.0.0.1";  // 局域网用 0.0.0.0
    uint16_t port = 4533;                 // 0 = 让系统挑（测试用）
  };

  // rig 由调用方给（mock / 串口），网关负责 open/close。
  CivTcpGateway(Config cfg, std::shared_ptr<ICivTransport> rig);
  ~CivTcpGateway();
  CivTcpGateway(const CivTcpGateway&) = delete;
  CivTcpGateway& operator=(const CivTcpGateway&) = delete;

  bool start(std::string* error);
  void stop();

  bool listening() const;
  uint16_t port() const;             // 实际绑定端口（port=0 时给测试用）
  bool clientConnected() const { return client_ != nullptr; }
  const std::string& log() const { return log_; }

  // 搬一次字节：客户端→电台原样写，电台→客户端按帧写。返回本次投递的帧数。
  size_t pump();

 private:
  void acceptPending();
  void onRigFrame(std::span<const uint8_t> frame);

  Config cfg_;
  std::shared_ptr<ICivTransport> rig_;
  QTcpServer* server_ = nullptr;
  QTcpSocket* client_ = nullptr;
  QByteArray rx_;  // 客户端→电台的半帧缓冲（TCP 会把多条粘成一包）
  std::string log_;
};

}  // namespace radio::core
