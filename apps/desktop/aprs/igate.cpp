// SPDX-License-Identifier: AGPL-3.0-or-later
#include "aprs/igate.h"

#include <algorithm>
#include <cctype>
#include <vector>

namespace radio::aprs {
namespace {
std::vector<std::string_view> splitPath(std::string_view via) {
  std::vector<std::string_view> out;
  size_t start = 0;
  while (start <= via.size()) {
    const size_t pos = via.find(',', start);
    if (pos == std::string_view::npos) {
      if (start < via.size()) out.push_back(via.substr(start));
      break;
    }
    if (pos > start) out.push_back(via.substr(start, pos - start));
    start = pos + 1;
  }
  return out;
}

bool isQConstruct(std::string_view s) {
  return s.size() >= 3 && s.size() <= 4 && (s[0] == 'q' || s[0] == 'Q');
}

std::string upperCopy(std::string_view s) {
  std::string out(s);
  std::transform(out.begin(), out.end(), out.begin(),
                 [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
  return out;
}
}  // namespace

bool isRfPacket(const Packet& p) {
  if (!p.qconstruct.empty()) return false;
  for (auto seg : splitPath(p.via)) {
    if (seg == "TCPIP" || seg == "TCPIP*" || seg == "TCPXX" || seg == "TCPXX*")
      return false;
  }
  return true;
}

bool isIsPacket(const Packet& p) { return !isRfPacket(p); }

std::string toIsPacket(const Packet& p, std::string_view igate_call) {
  if (!p.qconstruct.empty()) return p.raw_line;
  std::string out = p.source + ">" + p.dest;
  if (!p.via.empty()) out += "," + p.via;
  out += ",qAR," + upperCopy(igate_call);
  out += ":" + p.raw_info;
  return out;
}

bool mayGateToRf(const Packet& p, const IgatePolicy& policy) {
  if (!policy.is_to_rf || policy.callsign.empty()) return false;
  if (!isIsPacket(p)) return false;
  if (p.kind == PacketKind::ThirdParty && !policy.gate_third_party) return false;
  // IS→RF 只放消息/对象/条目：位置/气象/遥测属于当地 RF 台站该自己发的流量，
  // 全网转发会把外地台站灌进本地频率（New-N iGate 实践，与交接文档口径一致）。
  if (p.kind != PacketKind::Message && p.kind != PacketKind::Object &&
      p.kind != PacketKind::Item)
    return false;
  if (p.raw_info.empty()) return false;
  return true;
}

std::string toRfPacket(const Packet& p, const IgatePolicy& policy) {
  if (!mayGateToRf(p, policy)) return {};
  std::string out = p.source + ">" + p.dest;
  if (!policy.tx_path.empty()) out += "," + policy.tx_path;
  out += ":" + p.raw_info;
  return out;
}

std::string thirdPartyPacket(const Packet& p, std::string_view igate_call) {
  std::string inner = p.source + ">" + p.dest;
  if (!p.via.empty()) inner += "," + p.via;
  inner += ":" + p.raw_info;
  return upperCopy(igate_call) + ">APRS,TCPIP*:}" + inner;
}

std::string dedupKey(const Packet& p) {
  return upperCopy(p.source) + "|" + p.raw_info;
}

DuplicateFilter::DuplicateFilter(int window_s, size_t capacity)
    : window_ms_(static_cast<int64_t>(window_s) * 1000),
      capacity_(capacity == 0 ? 1 : capacity) {}

bool DuplicateFilter::duplicate(const std::string& key, int64_t now_ms) {
  auto it = seen_.find(key);
  if (it != seen_.end() && now_ms - it->second <= window_ms_)
    return true;  // 窗口从首见起算，不因重复刷新而无限延长
  seen_[key] = now_ms;
  if (seen_.size() > capacity_) prune(now_ms);
  return false;
}

void DuplicateFilter::prune(int64_t now_ms) {
  for (auto it = seen_.begin(); it != seen_.end();) {
    if (now_ms - it->second > window_ms_)
      it = seen_.erase(it);
    else
      ++it;
  }
  // 极端情况下仍超限：删最早的一半，避免无界增长
  if (seen_.size() > capacity_) {
    size_t drop = seen_.size() / 2;
    for (auto it = seen_.begin(); drop > 0 && it != seen_.end(); --drop)
      it = seen_.erase(it);
  }
}

void DuplicateFilter::clear() { seen_.clear(); }

}  // namespace radio::aprs
