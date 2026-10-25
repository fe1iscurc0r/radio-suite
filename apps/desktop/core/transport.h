// SPDX-License-Identifier: AGPL-3.0-or-later
// radio-suite · core/transport.h — CI-V 传输层抽象
//
// M0：mock（内存回环，供 CI 与 spike）。
// M1：udp（对齐 vendor/rsba1-core 的 RS-BA1 V2 UDP 信道）、serial（QSerialPort）。
// 多收单发（SourceManager）在 M1 落地：多个 RX 源并存，TX 只走用户指定的单源。
#pragma once

#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <vector>

namespace radio::core {

class ICivTransport {
 public:
  using FrameCallback = std::function<void(std::span<const uint8_t>)>;

  virtual ~ICivTransport() = default;

  virtual bool open(std::string* error) = 0;
  virtual void close() = 0;
  virtual bool isOpen() const = 0;

  // 发送完整 CI-V 帧（含 FE 前导与 FD 尾）。返回链路层是否成功。
  virtual bool send(std::span<const uint8_t> frame) = 0;

  // 排空待投递的应答并同步触发 onFrame（mock 队列 / M1 UDP 接收缓冲），
  // 返回本次投递条数。自带读线程、主动回调的事件式传输可保留默认实现。
  virtual size_t poll() { return 0; }

  void setFrameCallback(FrameCallback cb) { on_frame_ = std::move(cb); }

 protected:
  void emitFrame(std::span<const uint8_t> f) {
    if (on_frame_) on_frame_(f);
  }
  FrameCallback on_frame_;

  // 供子类拼接应答（避免各处手写 FE..FD）
  static std::vector<uint8_t> frame(uint8_t to, uint8_t from, uint8_t cmd,
                                    std::span<const uint8_t> payload);
};

}  // namespace radio::core
