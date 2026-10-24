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
