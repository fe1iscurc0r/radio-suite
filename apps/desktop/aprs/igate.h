// SPDX-License-Identifier: AGPL-3.0-or-later
// radio-suite · aprs/igate.h — RF↔APRS-IS 网关策略（去重、q 构造、双向门控）
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>

#include "aprs/aprs.h"

namespace radio::aprs {

struct IgatePolicy {
  std::string callsign;  // 网关呼号（写 q 构造/第三方包）
  bool rf_to_is = true;
  bool is_to_rf = false;
  bool gate_third_party = false;
  int dedup_window_s = 30;
  std::string tx_path = "WIDE1-1,WIDE2-1";  // IS→RF 重写路径
};

// RF 包判定：路径中没有 TCPIP/TCPXX，也没有 q 构造
bool isRfPacket(const Packet& p);
bool isIsPacket(const Packet& p);

// 收到的 RF 包转 APRS-IS 文本：追加 ,qAR,GATE
std::string toIsPacket(const Packet& p, std::string_view igate_call);
// IS 包转 RF 文本：重写路径；不允许时返回空串
bool mayGateToRf(const Packet& p, const IgatePolicy& policy);
std::string toRfPacket(const Packet& p, const IgatePolicy& policy);
// 第三方包包装（iGate 注入 IS）
std::string thirdPartyPacket(const Packet& p, std::string_view igate_call);

std::string dedupKey(const Packet& p);

class DuplicateFilter {
 public:
  explicit DuplicateFilter(int window_s = 30, size_t capacity = 2048);

  // 首次见到返回 false；窗口内重复返回 true（并刷新时间）
  bool duplicate(const std::string& key, int64_t now_ms);
  void clear();
  size_t size() const { return seen_.size(); }

 private:
  void prune(int64_t now_ms);

  int64_t window_ms_;
  size_t capacity_;
  std::unordered_map<std::string, int64_t> seen_;
};

}  // namespace radio::aprs
