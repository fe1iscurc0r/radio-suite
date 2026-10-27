// SPDX-License-Identifier: AGPL-3.0-or-later
#include "core/state.h"

namespace radio::core {

uint64_t effectiveTxHz(const RadioState& s) {
  const int64_t rx = static_cast<int64_t>(s.freq_hz);
  const int64_t off = static_cast<int64_t>(s.offset_hz);
  if (s.duplex_raw == 0x11 && rx >= off) return static_cast<uint64_t>(rx - off);
  if (s.duplex_raw == 0x12) return static_cast<uint64_t>(rx + off);
  return s.freq_hz;
}

void RadioStateStore::update(const RadioState& s) {
  std::map<uint64_t, Subscriber> to_notify;
  {
    std::lock_guard<std::mutex> lock(mu_);
    state_ = s;
    to_notify = subs_;  // 拷贝出来再回调，避免回调内 subscribe/unsubscribe 死锁
  }
  for (const auto& [id, cb] : to_notify)
    if (cb) cb(s);
}

RadioState RadioStateStore::snapshot() const {
  std::lock_guard<std::mutex> lock(mu_);
  return state_;
}

uint64_t RadioStateStore::subscribe(Subscriber cb) {
  std::lock_guard<std::mutex> lock(mu_);
  uint64_t id = next_id_++;
  subs_.emplace(id, std::move(cb));
  return id;
}

void RadioStateStore::unsubscribe(uint64_t id) {
  std::lock_guard<std::mutex> lock(mu_);
  subs_.erase(id);
}

}  // namespace radio::core
