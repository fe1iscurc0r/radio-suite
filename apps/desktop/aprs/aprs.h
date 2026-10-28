// SPDX-License-Identifier: AGPL-3.0-or-later
// radio-suite · aprs/aprs.h — APRS 协议核心（纯标准库，无 Qt）
//
// 覆盖 TNC2 文本包与 AX.25/KISS 二进制包的解析与生成：未压缩/压缩位置、
// Mic-E、对象/条目、天气、遥测、状态、查询、消息、第三方包。坐标一律换算成
// 十进制度，供台站表/地图直接使用（不再保留 DDMM.mm 字符串）。
#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace radio::aprs {

constexpr double kUnknownCoord = 1000.0;

struct LatLon {
  double lat = kUnknownCoord;
  double lon = kUnknownCoord;
  bool valid() const {
    return lat >= -90.0 && lat <= 90.0 && lon >= -180.0 && lon <= 180.0;
  }
};

struct Symbol {
  char table = '/';  // '/' 主表、'\' 副表、或 overlay 字符
  char code = '/';
};

struct Weather {
  bool present = false;
  bool has_wind = false;
  double wind_dir_deg = 0.0;
  double wind_kt = 0.0;
  double gust_kt = 0.0;
  bool has_temp = false;
  double temp_c = 0.0;
  bool has_rain_1h = false;
  double rain_1h_mm = 0.0;
  bool has_rain_24h = false;
  double rain_24h_mm = 0.0;
  bool has_humidity = false;
  int humidity = 0;
  bool has_pressure = false;
  double pressure_hpa = 0.0;
};

struct Telemetry {
  bool present = false;
  int seq = -1;
  double analog[5] = {};
  uint8_t digital = 0;
};

struct AprsMessage {
  std::string addressee;  // 9 字符，已去尾空格
  std::string text;
  std::string id;  // `{` 后的消息号（1-5 字符）
  bool ack = false;
  bool rej = false;
  bool bulletin = false;
  int line = 0;  // 回复行号（文本 `}` 前缀），0 = 无
};

enum class PacketKind {
  Unknown,
  Position,
  Object,
  Item,
  Weather,
  Telemetry,
  Status,
  Query,
  Message,
  ThirdParty,
};

enum class MicEStatus {
  Emergency,
  Priority,
  Special,
  Committed,
  Returning,
  InService,
  EnRoute,
  OffDuty,
  Custom,
  Unknown,
};

struct Packet {
  std::string source;
  std::string dest;
  std::string via;  // 原始中继路径（不含源/目的）
  std::string qconstruct;  // 路径中的 qXX（TCPIP*/qAR 等），无则空
  char dti = 0;            // 数据标识符（info 首字符）
  PacketKind kind = PacketKind::Unknown;

  bool has_position = false;
  bool compressed = false;
  bool mic_e = false;
  LatLon pos;
  Symbol symbol;
  double course_deg = -1.0;
  double speed_kt = -1.0;
  double altitude_ft = -1.0;
  std::string comment;
  std::string timestamp;  // 原始 7 字符时间戳（有则保留）

  Weather weather;
  Telemetry telemetry;
  AprsMessage message;

  std::string object_name;
  bool object_alive = true;

  std::string status;
  MicEStatus mic_e_status = MicEStatus::Unknown;
  bool messaging_capable = false;

  std::string raw_info;
  std::string raw_line;  // 收到的完整 TNC2 行

  bool hasPosition() const { return has_position && pos.valid(); }
};

// ---- 解析 ----
// TNC2："SRC>DST,WIDE1-1,WIDE2*:info"
bool parseTnc2(std::string_view line, Packet* out, std::string* error = nullptr);
// 只解析 info（来源/目的/路径由调用方填好）
bool parseInfo(std::string_view info, Packet* out, std::string* error = nullptr);
// `}` 第三方包解包（最多 3 层，防递归炸弹）
std::optional<Packet> unwrapThirdParty(const Packet& p);

// ---- 地理 ----
double distanceKm(LatLon a, LatLon b);
double bearingDeg(LatLon from, LatLon to);
double normalizeBearing(double deg);
bool parseLatitude(std::string_view s, double* out);
bool parseLongitude(std::string_view s, double* out);
std::string formatLatitude(double lat);   // "3114.32N"
std::string formatLongitude(double lon);  // "12121.92E"
LatLon maidenheadToLatLon(std::string_view grid, bool* ok = nullptr);
std::string latLonToMaidenhead(LatLon p);

// ---- 生成（全部返回 info 字段，不含 SRC>DST:）----
std::string makePosition(LatLon p, Symbol sym, std::string_view comment,
                         bool compressed = false, bool messaging = true);
std::string makeObject(std::string_view name, LatLon p, Symbol sym,
                       std::string_view comment, bool alive, int64_t now_ms);
std::string makeStatus(std::string_view text);
std::string makeMessage(std::string_view to, std::string_view text,
                        std::string_view id);
std::string makeAck(std::string_view to, std::string_view id, bool reject = false);
std::string makeTnc2(std::string_view source, std::string_view dest,
                     std::string_view path, std::string_view info);

// APRS-IS 通行码（公开算法；只计算本人呼号）
uint16_t passcode(std::string_view callsign);

// ---- APRS-IS 过滤器（12 类：r m b p o t g u a e f d q）----
enum class FilterKind {
  Range,       // r/lat/lon/km
  MyRange,     // m/km
  Buddy,       // b/call1/call2
  Prefix,      // p/prefix
  Object,      // o/obj
  Type,        // t/poimqstu...
  Group,       // g/call（消息群组）
  Unproto,     // u/dest（未协议目的地址）
  Area,        // a/latN/lonW/latS/lonE
  Entry,       // e/call（只放行该站发出的包）
  Friend,      // f/call/km
  Digipeater,  // d/digi
  QConstruct,  // q/qAI,qAR...
};

struct FilterLine {
  FilterKind kind = FilterKind::Range;
  std::string arg;  // 已规范化，不含类型前缀
};

bool parseFilterLine(std::string_view text, FilterLine* out,
                     std::string* error = nullptr);
std::string filterToString(const FilterLine& f);
// 空格拼接；空列表返回空串
std::string buildFilter(const std::vector<FilterLine>& filters);

// ---- AX.25 / KISS ----
struct Ax25Address {
  std::string call;
  uint8_t ssid = 0;
  bool last = false;
  bool repeated = false;
};

struct Ax25Frame {
  Ax25Address dest;
  Ax25Address source;
  std::vector<Ax25Address> via;
  std::string info;
};

std::vector<uint8_t> encodeAx25(const Ax25Frame& f);
bool parseAx25(std::span<const uint8_t> bytes, Ax25Frame* out,
               std::string* error = nullptr);
std::string ax25ToTnc2(const Ax25Frame& f);
std::optional<Ax25Frame> tnc2ToAx25(const Packet& p, bool dest_is_lat = false);

std::vector<uint8_t> kissEncode(std::span<const uint8_t> payload,
                                uint8_t port = 0);
struct KissFrame {
  uint8_t port = 0;
  std::vector<uint8_t> data;  // 去掉 port 与 FEND 的 AX.25 字节
};
// 流式解码：feed 任意切片，pop 取完整帧
class KissDecoder {
 public:
  void feed(std::span<const uint8_t> bytes);
  bool pop(KissFrame* out);
  void reset();
  size_t dropped() const { return dropped_; }

 private:
  bool in_frame_ = false;
  bool escape_ = false;
  bool overflow_ = false;
  std::vector<uint8_t> buf_;
  std::vector<KissFrame> ready_;
  size_t dropped_ = 0;
};

constexpr int kMaxAx25Bytes = 512;

}  // namespace radio::aprs
