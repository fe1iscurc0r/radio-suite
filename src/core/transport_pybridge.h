// SPDX-License-Identifier: AGPL-3.0-or-later
// radio-suite · core/transport_pybridge.h — rsba1-core（Python，MIT，真机验证）桥传输
//
// 设计：tools/rsba1_bridge.py 子进程，stdio 一行一个 JSON：
//   → {"id":N,"cmd":"open"|"civ"|"close", "hex":...}
//   ← {"id":N,"ok":true,"hex":...}
// rsba1-core 是目前唯一真机验证过的链路；C++ 原生 UDP transport（transport_udp）
// 与本桥并存，真机验收通过前两者互为备份。
#pragma once

#include <cstdint>

#include "core/transport.h"

class QProcess;

namespace radio::core {

class PyBridgeTransport final : public ICivTransport {
 public:
  struct Config {
    std::string host;
    std::string username;
    std::string password;
    std::string python_exe = "python";  // 运行环境可换真身路径
    int open_timeout_ms = 15000;
  };

  explicit PyBridgeTransport(Config cfg);
  ~PyBridgeTransport() override;

  bool open(std::string* error) override;
  void close() override;
  bool isOpen() const override;
  bool send(std::span<const uint8_t> frame) override;  // 异步：应答经 poll() 回来
  size_t poll() override;

  const std::string& lastLog() const { return log_; }

 private:
  void sendJson(const std::string& line);
  void drainStdout();
  static std::string findBridgeScript();

  Config cfg_;
  QProcess* proc_ = nullptr;
  bool open_ = false;
  uint64_t next_id_ = 1;
  uint64_t pending_civ_id_ = 0;  // 最近一次 civ 请求（多并发时 M1 不需要）
  size_t delivered_ = 0;
  std::string log_;
};

}  // namespace radio::core
