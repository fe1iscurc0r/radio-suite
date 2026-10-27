// SPDX-License-Identifier: AGPL-3.0-or-later
// radio-suite · core/state.h — RadioState 状态单源（住在 core 线程，UI 只订阅）
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <vector>

#include "core/civ.h"

namespace radio::core {

struct RadioState {
  uint64_t freq_hz = 0;
  civ::Mode mode = civ::Mode::USB;
  int filter = 0;
  bool ptt_on = false;
  int smeter_raw = 0;
  float smeter_dbm = -999.0f;
  // 复合表（0x1A 0x11；显示量纲待真机标定，暂 po=raw×0.1W, swr=raw×0.1）
  float po_w = 0.0f;
  float swr = 0.0f;
  int alc = 0;
  // 电台功能开关（0x11 / 0x16 族回读）
  bool att_20db = false;
  bool nb = false;
  bool nr = false;
  bool moni = false;
  bool notch_auto = false;
  bool vox = false;
  int pamp = 0;
  int agc = 2;
  int duplex_raw = 0;  // 0x0F 读回值：0x00=simplex(归一) 0x11=DUP- 0x12=DUP+
  bool split = false;  // 0x0F 0x00/0x01（Split）
  bool rit = false;    // 0x21 0x01
  bool dtx = false;    // 0x21 0x02（ΔTX）
  uint32_t offset_hz = 0;  // 中继频差（0x0C 读回；DUP± 时 TX=RX∓/±offset）
  // 亚音 / 数字亚音（0x16 0x5D/0x42/0x43/0x4B + 0x1B 0x00/0x01/0x02）
  int tone_squelch = 0;       // 0x16 0x5D 原始枚举（0/1/2/3/6/7/8/9）
  bool repeater_tone = false; // 0x16 0x42 中继音（CTCSS 发射）
  bool tsql = false;          // 0x16 0x43 亚音静噪
  bool dtcs_on = false;       // 0x16 0x4B 数字亚音开关
  uint16_t tone_decihz = 885; // 0x1B 0x00（0.1Hz 单位，885=88.5Hz）
  uint16_t tsql_decihz = 885; // 0x1B 0x01
  int dtcs_code = 23;         // 0x1B 0x02（三位八进制数字 023..754）
  int dtcs_rx_polarity = 0;
  int dtcs_tx_polarity = 0;
  // 双 VFO（0x25/0x26 读未选中 VFO；0x07 无回读，按命令本地记）
  uint64_t sub_freq_hz = 0;
  civ::Mode sub_mode = civ::Mode::LSB;
  int sub_filter = 0;
  bool selected_sub = false;
  // CW Keyer（0x14 原始电平 + 0x16 0x47）
  int cw_pitch_level = 0;
  int cw_key_speed_level = 0;
  int break_in_delay_level = 0;
  bool break_in = false;
  // Set Mode 菜单快照：key = (group << 8) | item
  std::map<uint16_t, std::vector<uint8_t>> menu_values;
  // 记忆信道：最近一次读回的内容 + 当前组/信道
  std::optional<civ::MemoryChannel> memory_channel;
  int memory_group = 0;
  int memory_index = 0;
  uint64_t last_update_ms = 0;  // 最后一次字段变更的单调时钟

  bool operator==(const RadioState&) const = default;
};

// 发射频率：DUP− 时 RX−offset，DUP+ 时 RX+offset，simplex 时 = RX。
// 频差只对当前选中 VFO（MAIN）生效；Split 的 TX 取自另一 VFO，另行处理。
uint64_t effectiveTxHz(const RadioState& s);

// 状态单源：写端是 transport 会话（core 侧）；UI/总线作为订阅者收快照。
// 回调在写入线程同步调用——跨线程消费方自行投递到自己的队列/事件循环（M1 引入）。
class RadioStateStore {
 public:
  using Subscriber = std::function<void(const RadioState&)>;

  void update(const RadioState& s);
  RadioState snapshot() const;

  uint64_t subscribe(Subscriber cb);
  void unsubscribe(uint64_t id);

 private:
  mutable std::mutex mu_;
  RadioState state_{};
  std::map<uint64_t, Subscriber> subs_;
  uint64_t next_id_ = 1;
};

}  // namespace radio::core
