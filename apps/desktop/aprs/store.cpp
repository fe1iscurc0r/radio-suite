// SPDX-License-Identifier: AGPL-3.0-or-later
#include "aprs/store.h"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace radio::aprs {
namespace {
std::string upperCopy(std::string_view s) {
  std::string out(s);
  std::transform(out.begin(), out.end(), out.begin(),
                 [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
  return out;
}

std::string_view trim(std::string_view s) {
  while (!s.empty() && s.front() == ' ') s.remove_prefix(1);
  while (!s.empty() && s.back() == ' ') s.remove_suffix(1);
  return s;
}
}  // namespace

bool callsignEqual(std::string_view a, std::string_view b) {
  return upperCopy(trim(a)) == upperCopy(trim(b));
}

std::string addresseeOf(std::string_view call) {
  const size_t dash = call.find('-');
  return upperCopy(dash == std::string_view::npos ? call : call.substr(0, dash));
}

StationStore::StationStore(size_t capacity, size_t track_capacity)
    : capacity_(capacity == 0 ? 1 : capacity),
      track_capacity_(track_capacity == 0 ? 1 : track_capacity) {}

std::string StationStore::ingest(const Packet& p, int64_t now_ms) {
  std::string key;
  bool is_object = false;
  if ((p.kind == PacketKind::Object || p.kind == PacketKind::Item) &&
      !p.object_name.empty()) {
    key = upperCopy(p.object_name);
    is_object = true;
  } else {
    key = upperCopy(p.source);
  }
  if (key.empty()) return {};

  auto it = stations_.find(key);
  if (it == stations_.end()) {
    Station st;
    st.key = key;
    st.first_ms = now_ms;
    it = stations_.emplace(key, std::move(st)).first;
  }
  Station& st = it->second;
  st.last_ms = now_ms;
  st.packets += 1;
  st.call = p.source.empty() ? key : p.source;
  st.via = p.via;
  st.last_info = p.raw_info;
  st.messaging = st.messaging || p.messaging_capable;
  if (is_object) {
    st.is_object = true;
    st.name = p.object_name;
    st.alive = p.object_alive;
  }
  if (!p.status.empty()) st.status = p.status;
  if (p.telemetry.present) st.telemetry = p.telemetry;
  if (p.weather.present) st.weather = p.weather;

  if (p.hasPosition()) {
    const bool moved = !st.has_pos ||
                       distanceKm(st.pos, p.pos) > 0.02;  // ~20 m
    st.pos = p.pos;
    st.has_pos = true;
    st.symbol = p.symbol;
    if (!p.comment.empty()) st.comment = p.comment;
    if (p.course_deg >= 0) st.course_deg = p.course_deg;
    if (p.speed_kt >= 0) st.speed_kt = p.speed_kt;
    if (p.altitude_ft > -1000) st.altitude_ft = p.altitude_ft;

    const bool stale = st.track.empty() || now_ms - st.track.back().at_ms >= 15000;
    if (moved || stale) {
      st.track.push_back({p.pos, now_ms});
      while (st.track.size() > track_capacity_) st.track.pop_front();
    }
  }

  evictIfNeeded();
  return key;
}

const Station* StationStore::find(std::string_view key) const {
  auto it = stations_.find(upperCopy(key));
  return it == stations_.end() ? nullptr : &it->second;
}

Station* StationStore::find(std::string_view key) {
  auto it = stations_.find(upperCopy(key));
  return it == stations_.end() ? nullptr : &it->second;
}

void StationStore::clear() { stations_.clear(); }

size_t StationStore::expire(int64_t now_ms, int64_t ttl_ms) {
  size_t removed = 0;
  for (auto it = stations_.begin(); it != stations_.end();) {
    if (now_ms - it->second.last_ms > ttl_ms) {
      it = stations_.erase(it);
      ++removed;
    } else {
      ++it;
    }
  }
  return removed;
}

void StationStore::setCapacity(size_t cap, bool trim_now) {
  capacity_ = cap == 0 ? 1 : cap;
  if (trim_now) evictIfNeeded();
}

void StationStore::evictIfNeeded() {
  while (stations_.size() > capacity_) {
    auto oldest = stations_.begin();
    for (auto it = stations_.begin(); it != stations_.end(); ++it)
      if (it->second.last_ms < oldest->second.last_ms) oldest = it;
    stations_.erase(oldest);
  }
}

void StationStore::restore(std::vector<Station> stations) {
  stations_.clear();
  for (auto& st : stations) {
    if (st.key.empty()) continue;
    st.key = upperCopy(st.key);
    while (st.track.size() > track_capacity_) st.track.pop_front();
    stations_[st.key] = std::move(st);
  }
  evictIfNeeded();
}

MessageStore::MessageStore(size_t inbox_cap, size_t outbox_cap)
    : inbox_cap_(inbox_cap == 0 ? 1 : inbox_cap),
      outbox_cap_(outbox_cap == 0 ? 1 : outbox_cap) {}

std::string MessageStore::nextId(uint32_t seq) {
  static constexpr char kAlnum[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
  std::string out(3, '0');
  for (int i = 2; i >= 0; --i) {
    out[static_cast<size_t>(i)] = kAlnum[seq % 36];
    seq /= 36;
  }
  return out;
}

bool MessageStore::ingest(const Packet& p, std::string_view my_call,
                          int64_t now_ms) {
  if (p.kind != PacketKind::Message) return false;
  if (!callsignEqual(p.message.addressee, my_call)) return false;

  if (p.message.ack || p.message.rej) {
    acknowledge(p.message.id, p.message.rej);
    return false;
  }
  if (p.message.text.empty() && p.message.id.empty()) return false;

  InboxMessage m;
  m.from = p.source;
  m.to = std::string(my_call);
  m.text = p.message.text;
  m.id = p.message.id;
  m.line = p.message.line;
  m.at_ms = now_ms;
  inbox_.push_back(std::move(m));
  while (inbox_.size() > inbox_cap_) inbox_.erase(inbox_.begin());
  return true;
}

std::string MessageStore::enqueue(std::string_view to, std::string_view text,
                                  int64_t now_ms) {
  OutboxMessage m;
  m.to = upperCopy(to);
  m.text = std::string(text);
  m.id = nextId(seq_++);
  m.created_ms = now_ms;
  outbox_.push_back(std::move(m));
  while (outbox_.size() > outbox_cap_) outbox_.erase(outbox_.begin());
  return outbox_.back().id;
}

bool MessageStore::acknowledge(std::string_view id, bool reject) {
  for (auto& m : outbox_) {
    if (m.id == id) {
      if (reject)
        m.rejected = true;
      else
        m.acked = true;
      return true;
    }
  }
  return false;
}

std::vector<const OutboxMessage*> MessageStore::due(int64_t now_ms,
                                                    int64_t timeout_ms,
                                                    int max_tries) const {
  std::vector<const OutboxMessage*> out;
  for (const auto& m : outbox_) {
    if (m.acked || m.rejected || !m.retry_enabled) continue;
    if (m.tries >= max_tries) continue;
    const int64_t last = m.tries == 0 ? m.created_ms : m.last_try_ms;
    if (now_ms - last >= timeout_ms) out.push_back(&m);
  }
  return out;
}

void MessageStore::noteTry(std::string_view id, int64_t now_ms) {
  for (auto& m : outbox_) {
    if (m.id == id) {
      m.last_try_ms = now_ms;
      ++m.tries;
      return;
    }
  }
}

const OutboxMessage* MessageStore::findOutbox(std::string_view id) const {
  for (const auto& m : outbox_)
    if (m.id == id) return &m;
  return nullptr;
}

int MessageStore::unread() const {
  return static_cast<int>(std::count_if(
      inbox_.begin(), inbox_.end(), [](const InboxMessage& m) { return !m.read; }));
}

bool MessageStore::markRead(size_t index) {
  if (index >= inbox_.size()) return false;
  inbox_[index].read = true;
  return true;
}

void MessageStore::markAllRead() {
  for (auto& m : inbox_) m.read = true;
}

void MessageStore::clearInbox() { inbox_.clear(); }

void MessageStore::restore(std::vector<InboxMessage> inbox,
                           std::vector<OutboxMessage> outbox) {
  inbox_ = std::move(inbox);
  outbox_ = std::move(outbox);
  while (inbox_.size() > inbox_cap_) inbox_.erase(inbox_.begin());
  while (outbox_.size() > outbox_cap_) outbox_.erase(outbox_.begin());
  uint32_t max_seq = 0;
  for (const auto& m : outbox_) {
    uint32_t v = 0;
    for (char c : m.id) {
      v *= 36;
      if (c >= '0' && c <= '9')
        v += static_cast<uint32_t>(c - '0');
      else if (c >= 'A' && c <= 'Z')
        v += static_cast<uint32_t>(c - 'A' + 10);
    }
    max_seq = std::max(max_seq, v + 1);
  }
  seq_ = max_seq;
}

}  // namespace radio::aprs
