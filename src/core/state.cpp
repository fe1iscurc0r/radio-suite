// SPDX-License-Identifier: AGPL-3.0-or-later
#include "core/state.h"

namespace radio::core {

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
