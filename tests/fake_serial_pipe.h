// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/fake_serial_pipe.h — 给 SerialCivTransport 用的内存「串口线」
//
// 台架上没有 CAT 线，但 SerialCivTransport 里会出错的部分（粘包、半包、脏前缀、
// 超长无尾、写失败、开线失败）跟有没有硬件完全无关，所以做成可注入的管道。
// 想跑真往返就把 responder 接一个 MockTransport：写进来的请求帧变成电台应答帧，
// 下一轮 poll 就被切出来——等于把一台模拟电台插在同一条虚拟线上。
#pragma once

#include <QByteArray>

#include <deque>
#include <functional>
#include <span>
#include <string>
#include <vector>

#include "core/transport_serial.h"

namespace radio::test {

class FakeSerialPipe final : public radio::core::ICivBytePipe {
 public:
  bool open(std::string* error) override {
    if (fail_open) {
      if (error) *error = "fake: port busy";
      return false;
    }
    opened_ = true;
    return true;
  }
  void close() override { opened_ = false; }
  bool isOpen() const override { return opened_; }

  bool write(std::span<const uint8_t> bytes) override {
    if (!opened_ || fail_write) return false;
    written_.insert(written_.end(), bytes.begin(), bytes.end());
    // 传「刚写的这一段」而不是全部历史：transport 每次 send 就是一条完整帧，
    // 电台侧要的就是这一条，翻历史只会让 responder 自己再切一遍
    if (responder) responder(bytes);
    return true;
  }

  QByteArray readAvailable() override {
    QByteArray out;
    while (!pending_.empty()) {
      out.append(static_cast<char>(pending_.front()));
      pending_.pop_front();
    }
    return out;
  }

  // ---- 测试侧操纵 ----
  void feed(std::span<const uint8_t> bytes) {
    pending_.insert(pending_.end(), bytes.begin(), bytes.end());
  }
  void feed(const std::vector<uint8_t>& bytes) { feed(std::span<const uint8_t>(bytes)); }
  void clearWritten() { written_.clear(); }
  const std::vector<uint8_t>& written() const { return written_; }

  bool fail_open = false;
  bool fail_write = false;
  // 每次 write 后调用，参数是刚写进去的那一段（一条完整 CI-V 请求帧）
  std::function<void(std::span<const uint8_t>)> responder;

 private:
  bool opened_ = false;
  std::deque<uint8_t> pending_;
  std::vector<uint8_t> written_;
};

}  // namespace radio::test
