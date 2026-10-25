// SPDX-License-Identifier: AGPL-3.0-or-later
// radio-suite · core/state.h — RadioState 状态单源（住在 core 线程，UI 只订阅）
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <mutex>

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
  uint64_t last_update_ms = 0;  // 最后一次字段变更的单调时钟

  bool operator==(const RadioState&) const = default;
};

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
