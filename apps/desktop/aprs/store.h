// SPDX-License-Identifier: AGPL-3.0-or-later
// radio-suite · aprs/store.h — 台站聚合与消息收件箱（纯标准库，时间由调用方注入）
//
// 台站按呼号/对象名聚合：数值坐标、轨迹点、天气、包计数与最后收到时间；
// 消息层实现 APRS 消息号、ACK/REJ、超时重发与未读计数。
#pragma once

#include <cstdint>
#include <deque>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "aprs/aprs.h"

namespace radio::aprs {

struct TrackPoint {
  LatLon pos;
  int64_t at_ms = 0;
};

struct Station {
  std::string key;         // 呼号，或对象/条目名
  std::string call;        // 发包台站（对象的上报者）
  std::string name;        // 对象/条目名（非对象为空）
  bool is_object = false;
  bool alive = true;

  LatLon pos;
  bool has_pos = false;
  Symbol symbol;
  std::string comment;
  std::string status;
  std::string via;
  std::string last_info;

  int packets = 0;
  int64_t first_ms = 0;
  int64_t last_ms = 0;

  double course_deg = -1.0;
  double speed_kt = -1.0;
  double altitude_ft = -1.0;
  Weather weather;
  Telemetry telemetry;
  bool messaging = false;

  std::deque<TrackPoint> track;

  bool hasPosition() const { return has_pos && pos.valid(); }
};

class StationStore {
 public:
  explicit StationStore(size_t capacity = 1000, size_t track_capacity = 120);

  // 返回聚合键；无效包返回空串
  std::string ingest(const Packet& p, int64_t now_ms);

  const Station* find(std::string_view key) const;
  Station* find(std::string_view key);
  const std::map<std::string, Station>& all() const { return stations_; }
  size_t size() const { return stations_.size(); }
  void clear();

  // 删除 last_ms 早于 now-ttl 的台站，返回删除数
  size_t expire(int64_t now_ms, int64_t ttl_ms);
  size_t capacity() const { return capacity_; }
  void setCapacity(size_t cap, bool trim_now = true);
  // 持久化快照恢复：清空后按 key 覆盖载入，再按容量裁剪
  void restore(std::vector<Station> stations);

 private:
  void evictIfNeeded();

  std::map<std::string, Station> stations_;
  size_t capacity_;
  size_t track_capacity_;
};

struct InboxMessage {
  std::string from;
  std::string to;  // 本地呼号
  std::string text;
  std::string id;
  int line = 0;
  int64_t at_ms = 0;
  bool read = false;
};

struct OutboxMessage {
  std::string to;
  std::string text;
  std::string id;
  int64_t created_ms = 0;
  int64_t last_try_ms = 0;
  int tries = 0;
  bool acked = false;
  bool rejected = false;
  bool retry_enabled = true;
};

class MessageStore {
 public:
  MessageStore(size_t inbox_cap = 500, size_t outbox_cap = 200);

  // 收包：发给 my_call 的消息进收件箱；ack/rej 更新发件箱（都不进收件箱之外）。
  // 返回是否产生了新的收件箱条目。
  bool ingest(const Packet& p, std::string_view my_call, int64_t now_ms);

  std::string enqueue(std::string_view to, std::string_view text,
                      int64_t now_ms);
  bool acknowledge(std::string_view id, bool reject);
  // 待重发：超时、未确认、未超次数
  std::vector<const OutboxMessage*> due(int64_t now_ms, int64_t timeout_ms,
                                        int max_tries) const;
  void noteTry(std::string_view id, int64_t now_ms);

  const std::vector<InboxMessage>& inbox() const { return inbox_; }
  const std::vector<OutboxMessage>& outbox() const { return outbox_; }
  const OutboxMessage* findOutbox(std::string_view id) const;

  int unread() const;
  bool markRead(size_t index);
  void markAllRead();
  void clearInbox();
  // 持久化快照恢复：覆盖收件箱/发件箱，并让新消息号从已有最大号之后继续
  void restore(std::vector<InboxMessage> inbox,
               std::vector<OutboxMessage> outbox);

  static std::string nextId(uint32_t seq);  // 3 位 base36

 private:
  std::vector<InboxMessage> inbox_;
  std::vector<OutboxMessage> outbox_;
  size_t inbox_cap_;
  size_t outbox_cap_;
  uint32_t seq_ = 0;
};

// 呼号比较：大小写不敏感，SSID 必须一致
bool callsignEqual(std::string_view a, std::string_view b);
std::string addresseeOf(std::string_view call);  // 去 SSID，大写

}  // namespace radio::aprs
