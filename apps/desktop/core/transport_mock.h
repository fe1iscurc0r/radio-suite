// SPDX-License-Identifier: AGPL-3.0-or-later
// radio-suite · core/transport_mock.h — 内存 IC-705 模拟器（CI / spike / 界面开发用）
#pragma once

#include <map>
#include <string>
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
    // 电台功能状态（0x16 族 + ATT），UI 开关经 mock 状态回读
    bool att_20db = false;   // 0x11：仅 1.8–54 MHz 接受
    bool nb = false;         // 0x16 0x22
    bool nr = false;         // 0x16 0x40
    bool moni = false;       // 0x16 0x45
    bool notch_auto = false; // 0x16 0x41
    bool vox = false;        // 0x16 0x46
    int pamp = 0;            // 0x16 0x02：0/1/2
    int agc = 2;             // 0x16 0x12：1=fast/2=mid/3=slow
    int duplex = 0x10;       // 0x0F：0x10 simplex/0x11 dup-/0x12 dup+（读回 simplex 归一 0x00）
    bool split = false;      // 0x0F 另一值域：0x00/0x01
    bool rit = false;        // 0x21 0x01
    bool dtx = false;        // 0x21 0x02（ΔTX）
    uint32_t offset_hz = 600'000;  // 中继频差（0x0C/0x0D；默认 2m 惯例 600k）
    // 双 VFO：freq_hz/mode/filter 视作 MAIN(A)，以下为 SUB(B)
    uint64_t sub_freq_hz = 7'074'000;
    civ::Mode sub_mode = civ::Mode::LSB;
    int sub_filter = 1;
    int sub_data_mode = 0;
    bool selected_sub = false;  // 0x07：当前选中 VFO（false=MAIN）
    // CW Keyer
    int key_speed_level = 26;      // 0x14 0x0C raw 0..255（26 ≈ 10 WPM）
    int cw_pitch_level = 102;      // 0x14 0x09 raw（102 ≈ 600 Hz）
    int break_in_delay_level = 26; // 0x14 0x0F raw
    bool break_in = false;         // 0x16 0x47
    std::string cw_text;           // 0x17 累积的待发文本（测试断言用）
    bool cw_sending = false;       // 0x17 文本在途；0xFF 清除
    // Set Mode 菜单项：key = (group << 8) | item
    std::map<uint16_t, std::vector<uint8_t>> menu;
    // 记忆信道：channel → 内容（默认空，测试自行 setSim/写入）
    std::map<int, civ::MemoryChannel> memory;
    int memory_group = 1;
    int memory_channel = 1;
  };

  // 注：默认实参 `SimConfig cfg = {}` 在 GCC/Clang 下不合法
  // （类内默认成员初始化器在类外使用），故用重载而非默认实参。
  MockTransport();
  explicit MockTransport(SimConfig cfg);

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
