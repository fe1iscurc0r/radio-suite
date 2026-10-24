// SPDX-License-Identifier: AGPL-3.0-or-later
// radio-suite · core/transport_mock.h — 内存 IC-705 模拟器（CI / spike / 界面开发用）
#pragma once

#include <vector>

#include "core/civ.h"
#include "core/transport.h"

namespace radio::core {

// 行为对齐参考实现与真机报告：
//   - 读频率/模式/S-meter/PTT 应答；设频率/模式/PTT 回 ACK(0xFB) 或 NAK(0xFA)
//   - 设频率走 bands.cpp 白名单（越界 NAK，模拟真机拒答语义）
class MockTransport final : public ICivTransport {
 public:
  struct SimConfig {
    uint64_t freq_hz = 14'270'000;
    civ::Mode mode = civ::Mode::USB;
    int filter = 1;
    bool ptt = false;
    int smeter_raw = 42;
    uint8_t radio_addr = civ::kIc705Addr;
  };

  explicit MockTransport(SimConfig cfg = {});

  bool open(std::string* error) override;
  void close() override;
  bool isOpen() const override;
  bool send(std::span<const uint8_t> frame) override;

  // 排空应答队列并触发 onFrame 回调（避免 send 内重入）。返回应答条数。
  size_t poll();

  const SimConfig& sim() const { return sim_; }
  void setSim(const SimConfig& cfg) { sim_ = cfg; }

 private:
  void pushResponse(uint8_t cmd, std::span<const uint8_t> payload);

  SimConfig sim_;
  bool open_ = false;
  std::vector<std::vector<uint8_t>> pending_;  // 待 poll 的应答帧
  uint8_t last_controller_ = civ::kDefaultControllerAddr;
};

}  // namespace radio::core
