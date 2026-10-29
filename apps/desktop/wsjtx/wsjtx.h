// SPDX-License-Identifier: AGPL-3.0-or-later
// radio-suite · wsjtx/wsjtx.h — WSJT-X UDP 消息协议（schema 3，纯标准库）
//
// 字段布局以 WSJT-X 上游 Network/NetworkMessage.hpp 的注释为准：
// magic 0xadbccbda + schema + type + id(utf8→UTF-16BE)，
// 整数大端；QTime 为当日毫秒；QDateTime 为 Qt5 序列化
// (qint64 Julian day + quint32 当日毫秒 + quint8 timeSpec [+ qint32 偏移])。
#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace radio::wsjtx {

constexpr uint32_t kMagic = 0xadbccbda;
constexpr uint32_t kSchema = 3;
constexpr uint16_t kDefaultPort = 2237;
constexpr const char* kDefaultGroup = "224.0.0.1";

enum class MessageType : uint32_t {
  Heartbeat = 0,
  Status = 1,
  Decode = 2,
  Clear = 3,
  Reply = 4,
  QsoLogged = 5,
  Close = 6,
  Replay = 7,
  HaltTx = 8,
  FreeText = 9,
  WsprDecode = 10,
  Location = 11,
  LoggedAdif = 12,
  HighlightCallsign = 13,
  SwitchConfiguration = 14,
  Configure = 15,
  AnnotationInfo = 16,
};

const char* messageTypeName(MessageType type);

// Qt5 QDataStream 的 QDateTime：UTC 为 spec=1；spec=0 按 UTC 解释
// （WSJT-X 的 QSO 时间实际按 UTC 发送）。
struct DateTime {
  int64_t julian_day = 0;
  uint32_t msecs = 0;
  uint8_t spec = 1;
  int32_t offset_sec = 0;

  int64_t utcMs() const;
  static DateTime fromUtcMs(int64_t ms);
};

struct Heartbeat {
  uint32_t max_schema = kSchema;
  std::string version;
  std::string revision;
};

struct Status {
  uint64_t dial_frequency = 0;
  std::string mode;
  std::string dx_call;
  std::string report;
  std::string tx_mode;
  bool tx_enabled = false;
  bool transmitting = false;
  bool decoding = false;
  uint32_t rx_df = 0;
  uint32_t tx_df = 0;
  std::string de_call;
  std::string de_grid;
  std::string dx_grid;
  bool tx_watchdog = false;
  std::string sub_mode;
  bool fast_mode = false;
  uint8_t special_operation_mode = 0;
  uint32_t frequency_tolerance = 0xffffffffu;
  uint32_t tr_period = 0xffffffffu;
  std::string configuration_name;
  std::string tx_message;
};

struct Decode {
  bool is_new = true;
  uint32_t time_ms = 0;  // 当日毫秒（QTime）
  int32_t snr = 0;
  double delta_time = 0.0;
  int32_t delta_frequency = 0;  // 协议为有符号 qint32
  std::string mode;
  std::string message;
  bool low_confidence = false;
  bool off_air = false;
};

struct Clear {
  uint8_t window = 0;
};

struct Reply {
  uint32_t time_ms = 0;
  int32_t snr = 0;
  double delta_time = 0.0;
  int32_t delta_frequency = 0;  // 协议为有符号 qint32
  std::string mode;
  std::string message;
  bool low_confidence = false;
  uint8_t modifiers = 0;
};

struct QsoLogged {
  DateTime date_time_off;
  std::string dx_call;
  std::string dx_grid;
  uint64_t tx_frequency = 0;
  std::string mode;
  std::string report_sent;
  std::string report_received;
  std::string tx_power;
  std::string comments;
  std::string name;
  DateTime date_time_on;
  std::string operator_call;
  std::string my_call;
  std::string my_grid;
  std::string exchange_sent;
  std::string exchange_received;
  std::string adif_propagation;
};

struct Close {};

struct Replay {};

struct HaltTx {
  bool auto_tx_only = false;
};

struct FreeText {
  std::string text;
  bool send = false;
};

struct WsprDecode {
  bool is_new = true;
  uint32_t time_ms = 0;
  int32_t snr = 0;
  double delta_time = 0.0;
  uint64_t frequency = 0;
  int32_t drift = 0;
  std::string callsign;
  std::string grid;
  int32_t power_dbm = 0;
  bool off_air = false;
};

struct Location {
  std::string grid;
};

struct LoggedAdif {
  std::string adif;
};

struct SwitchConfiguration {
  std::string name;
};

struct Configure {
  std::string mode;
  uint32_t frequency_tolerance = 0xffffffffu;
  std::string sub_mode;
  bool fast_mode = false;
  uint32_t tr_period = 0xffffffffu;
  uint32_t rx_df = 0xffffffffu;
  std::string dx_call;
  std::string dx_grid;
  bool generate_messages = false;
};

using Body = std::variant<Heartbeat, Status, Decode, Clear, Reply, QsoLogged, Close,
                          Replay, HaltTx, FreeText, WsprDecode, Location, LoggedAdif,
                          SwitchConfiguration, Configure>;

struct Message {
  MessageType type = MessageType::Heartbeat;
  std::string id;
  Body body;
};

// 序列化（含 magic/schema/type/id 前缀）
std::vector<uint8_t> encode(const Message& message);

// 解析一条完整的 UDP 报文；失败返回 false 并写 error
bool decode(std::span<const uint8_t> bytes, Message* out,
            std::string* error = nullptr);

// 常用上行消息构造（id 为接收方客户端 key）
Message makeHeartbeat(std::string id, std::string version = "radio-suite",
                      std::string revision = "");
Message makeReply(std::string id, const Decode& decode, uint8_t modifiers = 0);
Message makeHaltTx(std::string id, bool auto_tx_only = false);
Message makeFreeText(std::string id, std::string text, bool send = false);
Message makeReplay(std::string id);
Message makeLocation(std::string id, std::string grid);
Message makeClear(std::string id, uint8_t window = 0);
Message makeClose(std::string id);

}  // namespace radio::wsjtx
