// SPDX-License-Identifier: AGPL-3.0-or-later
#include "aprs/aprs.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>

namespace radio::aprs {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kEarthRadiusKm = 6371.0088;

char upper(char c) { return static_cast<char>(std::toupper(static_cast<unsigned char>(c))); }
char lower(char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }
bool isDigit(char c) { return c >= '0' && c <= '9'; }
bool isSpace(char c) { return c == ' '; }

std::string_view trim(std::string_view s) {
  while (!s.empty() && (s.front() == ' ' || s.front() == '\r' ||
                        s.front() == '\n' || s.front() == '\t'))
    s.remove_prefix(1);
  while (!s.empty() && (s.back() == ' ' || s.back() == '\r' ||
                        s.back() == '\n' || s.back() == '\t'))
    s.remove_suffix(1);
  return s;
}

std::string upperCopy(std::string_view s) {
  std::string out(s);
  std::transform(out.begin(), out.end(), out.begin(),
                 [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
  return out;
}

std::vector<std::string_view> split(std::string_view s, char sep) {
  std::vector<std::string_view> out;
  size_t start = 0;
  while (start <= s.size()) {
    const size_t pos = s.find(sep, start);
    if (pos == std::string_view::npos) {
      out.push_back(s.substr(start));
      break;
    }
    out.push_back(s.substr(start, pos - start));
    start = pos + 1;
  }
  return out;
}

int parseInt(std::string_view s) {
  int v = 0;
  for (char c : s) {
    if (!isDigit(c)) break;
    v = v * 10 + (c - '0');
  }
  return v;
}

// 位置正文：未压缩 19 字符或压缩 13 字符起
bool looksCompressed(std::string_view body) {
  if (body.empty()) return false;
  const char c = body[0];
  if (isDigit(c) || c == ' ') return false;  // 未压缩纬度开头
  return body.size() >= 13;
}

bool parseTimestamp(std::string_view s, std::string* out) {
  if (s.size() < 7) return false;
  for (int i = 0; i < 6; ++i)
    if (!isDigit(s[static_cast<size_t>(i)])) return false;
  const char z = s[6];
  if (z != 'z' && z != 'h' && z != '/') return false;
  if (out) *out = std::string(s.substr(0, 7));
  return true;
}

bool parseUncompressedPos(std::string_view body, Packet* p) {
  if (body.size() < 19) return false;
  const char ns = upper(body[7]);
  if (ns != 'N' && ns != 'S') return false;
  const char ew = upper(body[17]);
  if (ew != 'E' && ew != 'W') return false;
  if (body[4] != '.' || body[14] != '.') return false;
  for (int i : {0, 1, 2, 3, 5, 6})
    if (!isDigit(body[static_cast<size_t>(i)]) && body[static_cast<size_t>(i)] != ' ')
      return false;
  for (int i : {9, 10, 11, 12, 13, 15, 16})
    if (!isDigit(body[static_cast<size_t>(i)]) && body[static_cast<size_t>(i)] != ' ')
      return false;

  auto digitAt = [&](int i) {
    const char c = body[static_cast<size_t>(i)];
    return isDigit(c) ? c - '0' : 0;
  };
  const double lat = digitAt(0) * 10 + digitAt(1) +
                     (digitAt(2) * 10 + digitAt(3) + (digitAt(5) * 10 + digitAt(6)) / 100.0) / 60.0;
  const double lon = digitAt(9) * 100 + digitAt(10) * 10 + digitAt(11) +
                     (digitAt(12) * 10 + digitAt(13) +
                      (digitAt(15) * 10 + digitAt(16)) / 100.0) / 60.0;

  p->pos.lat = ns == 'S' ? -lat : lat;
  p->pos.lon = ew == 'W' ? -lon : lon;
  p->symbol.table = body[8];
  p->symbol.code = body[18];
  p->comment = std::string(trim(body.substr(19)));
  p->compressed = false;
  p->has_position = true;
  return true;
}

bool base91Value(std::string_view s, size_t offset, uint32_t* out) {
  if (s.size() < offset + 4) return false;
  uint32_t v = 0;
  for (size_t i = 0; i < 4; ++i) {
    const int c = static_cast<unsigned char>(s[offset + i]) - 33;
    if (c < 0 || c > 90) return false;
    v = v * 91 + static_cast<uint32_t>(c);
  }
  *out = v;
  return true;
}

bool parseCompressedPos(std::string_view body, Packet* p) {
  if (body.size() < 13) return false;
  uint32_t latv = 0, lonv = 0;
  if (!base91Value(body, 1, &latv) || !base91Value(body, 5, &lonv)) return false;
  p->pos.lat = 90.0 - static_cast<double>(latv) / 380926.0;
  p->pos.lon = static_cast<double>(lonv) / 190463.0 - 180.0;
  p->symbol.table = body[0];
  p->symbol.code = body[9];
  p->compressed = true;
  p->has_position = true;

  const char c = body[10];
  if (c != ' ') {
    // 压缩类型 c + 后续两字节携带高度/射程，s 位兼作 N/S
    if (p->pos.lat < 0) p->pos.lat = -p->pos.lat;
    const int alt_char = static_cast<unsigned char>(body[12]) - 33;
    if (alt_char >= 0) p->altitude_ft = static_cast<double>(alt_char) * 32.0 - 10000.0;
  }
  const size_t used = 13;
  p->comment = std::string(trim(body.substr(used)));
  return true;
}

bool parsePositionBody(std::string_view body, Packet* p) {
  if (looksCompressed(body)) return parseCompressedPos(body, p);
  return parseUncompressedPos(body, p);
}

// Mic-E 目的地址数字：0-9 正常，A-J 自定义消息位，P-Y 标准消息位
int micEDigit(char c, int mask, int* std_msg, int* cust_msg) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'A' && c <= 'J') {
    *cust_msg |= mask;
    return c - 'A';
  }
  if (c >= 'P' && c <= 'Y') {
    *std_msg |= mask;
    return c - 'P';
  }
  if (c == 'K') {
    *cust_msg |= mask;
    return 0;
  }
  if (c == 'L') return 0;
  if (c == 'Z') {
    *std_msg |= mask;
    return 0;
  }
  return 0;
}

bool parseMicE(std::string_view dest, std::string_view info, Packet* p) {
  if (dest.size() < 6 || info.size() < 9) return false;
  int std_msg = 0, cust_msg = 0;
  const int d0 = micEDigit(dest[0], 4, &std_msg, &cust_msg);
  const int d1 = micEDigit(dest[1], 2, &std_msg, &cust_msg);
  const int d2 = micEDigit(dest[2], 1, &std_msg, &cust_msg);
  const int d3 = micEDigit(dest[3], 0, &std_msg, &cust_msg);
  const int d4 = micEDigit(dest[4], 0, &std_msg, &cust_msg);
  const int d5 = micEDigit(dest[5], 0, &std_msg, &cust_msg);

  double lat = d0 * 10 + d1 + (d2 * 1000 + d3 * 100 + d4 * 10 + d5) / 6000.0;
  const char c3 = dest[3];
  if (isDigit(c3) || c3 == 'L') lat = -lat;  // 第 4 字符：南半球
  const bool offset = (dest[4] >= 'P' && dest[4] <= 'Z');

  const int lon0 = static_cast<unsigned char>(info[1]);
  const int lon1 = static_cast<unsigned char>(info[2]);
  const int lon2 = static_cast<unsigned char>(info[3]);

  double lon = kUnknownCoord;
  if (offset && lon0 >= 118 && lon0 <= 127)
    lon = lon0 - 118;
  else if (!offset && lon0 >= 38 && lon0 <= 127)
    lon = (lon0 - 38) + 10;
  else if (offset && lon0 >= 108 && lon0 <= 117)
    lon = (lon0 - 108) + 100;
  else if (offset && lon0 >= 38 && lon0 <= 107)
    lon = (lon0 - 38) + 110;

  if (lon < kUnknownCoord) {
    if (lon1 >= 88 && lon1 <= 97)
      lon += (lon1 - 88) / 60.0;
    else if (lon1 >= 38 && lon1 <= 87)
      lon += ((lon1 - 38) + 10) / 60.0;
    else
      lon = kUnknownCoord;
  }
  if (lon < kUnknownCoord && lon2 >= 28 && lon2 <= 127)
    lon += (lon2 - 28) / 6000.0;

  const char c5 = dest[5];
  if ((c5 >= 'P' && c5 <= 'Z') && lon < kUnknownCoord) lon = -lon;  // 第 6 字符：西经

  if (lat < -90.0 || lat > 90.0 || lon < -180.0 || lon > 180.0) return false;
  p->pos.lat = lat;
  p->pos.lon = lon;
  p->has_position = true;
  p->mic_e = true;

  const int sc0 = static_cast<unsigned char>(info[4]) - 28;
  const int sc1 = static_cast<unsigned char>(info[5]) - 28;
  const int sc2 = static_cast<unsigned char>(info[6]) - 28;
  if (sc0 >= 0 && sc1 >= 0 && sc2 >= 0) {
    p->speed_kt = sc0 * 10 + sc1 / 10;
    p->course_deg = (sc1 % 10) * 100 + sc2;
  }
  p->symbol.code = info[7];
  p->symbol.table = info[8];
  std::string_view rest = info.substr(9);
  if (rest.size() >= 5 && rest[0] == '"' && rest[4] == '}') {
    uint32_t altv = 0;
    if (base91Value(rest, 1, &altv)) p->altitude_ft = static_cast<double>(altv) - 10000.0;
    rest.remove_prefix(5);
  }
  p->comment = std::string(trim(rest));

  if (std_msg == 0 && cust_msg == 0)
    p->mic_e_status = MicEStatus::Emergency;
  else if (std_msg != 0 && cust_msg == 0)
    p->mic_e_status = static_cast<MicEStatus>(std_msg);
  else if (std_msg == 0 && cust_msg != 0)
    p->mic_e_status = MicEStatus::Custom;
  else
    p->mic_e_status = MicEStatus::Unknown;
  // Mic-E 消息位（A-J/P-Y）表示具备消息能力
  p->messaging_capable = (std_msg != 0 || cust_msg != 0);
  return true;
}

void parseWeatherComment(std::string_view comment, Weather* w) {
  bool any = false;
  for (size_t i = 0; i < comment.size(); ++i) {
    const char c = comment[i];
    const bool boundary = i == 0 || comment[i - 1] == ' ';
    if (boundary && isDigit(c) && i + 5 < comment.size() && comment[i + 3] == '/' &&
        isDigit(comment[i + 1]) && isDigit(comment[i + 2]) &&
        isDigit(comment[i + 4]) && isDigit(comment[i + 5])) {
      w->wind_dir_deg = parseInt(comment.substr(i, 3));
      w->wind_kt = parseInt(comment.substr(i + 4, 3));
      w->has_wind = true;
      any = true;
      i += 5;
      continue;
    }
    if (c == 'g' && i + 3 < comment.size()) {
      const auto v = comment.substr(i + 1, 3);
      if (isDigit(v[0]) && isDigit(v[1]) && isDigit(v[2])) {
        w->gust_kt = parseInt(v);
        w->has_wind = true;
        any = true;
        i += 3;
        continue;
      }
    }
    if (c == 't' && i + 3 < comment.size()) {
      const auto v = comment.substr(i + 1, 3);
      if ((isDigit(v[0]) || v[0] == '-') && isDigit(v[1]) && isDigit(v[2])) {
        const double f = v[0] == '-' ? -parseInt(v.substr(1, 2)) : parseInt(v);
        w->temp_c = (f - 32.0) * 5.0 / 9.0;  // APRS 天气温度单位是华氏
        w->has_temp = true;
        any = true;
        i += 3;
        continue;
      }
    }
    if (c == 'r' && i + 3 < comment.size()) {
      const auto v = comment.substr(i + 1, 3);
      if (isDigit(v[0]) && isDigit(v[1]) && isDigit(v[2])) {
        w->rain_1h_mm = parseInt(v) / 100.0;
        w->has_rain_1h = true;
        any = true;
        i += 3;
        continue;
      }
    }
    if (c == 'p' && i + 3 < comment.size()) {
      const auto v = comment.substr(i + 1, 3);
      if (isDigit(v[0]) && isDigit(v[1]) && isDigit(v[2])) {
        w->rain_24h_mm = parseInt(v) / 100.0;
        w->has_rain_24h = true;
        any = true;
        i += 3;
        continue;
      }
    }
    if (c == 'h' && i + 2 < comment.size()) {
      const auto v = comment.substr(i + 1, 2);
      if (isDigit(v[0]) && isDigit(v[1])) {
        w->humidity = parseInt(v);
        w->has_humidity = true;
        any = true;
        i += 2;
        continue;
      }
    }
    if (c == 'b' && i + 5 < comment.size()) {
      const auto v = comment.substr(i + 1, 5);
      bool ok = true;
      for (char d : v) ok = ok && isDigit(d);
      if (ok) {
        w->pressure_hpa = parseInt(v) / 10.0;
        w->has_pressure = true;
        any = true;
        i += 5;
        continue;
      }
    }
  }
  w->present = any;
}

void parseAltitudeFromComment(std::string* comment, Packet* p) {
  const size_t at = comment->find("/A=");
  if (at == std::string::npos || at + 9 > comment->size()) return;
  const auto v = std::string_view(*comment).substr(at + 3, 6);
  for (char c : v)
    if (!isDigit(c)) return;
  p->altitude_ft = parseInt(v);
  comment->erase(at, 9);  // 已解释的高度标记不再当注释显示
}

bool parseObjectInfo(std::string_view info, Packet* p) {
  if (info.size() < 9 + 1) return false;
  const std::string_view name = info.substr(1, 9);
  const char alive = info[10];
  if (alive != '*' && alive != '_') return false;
  p->object_name = std::string(trim(name));
  p->object_alive = alive == '*';
  p->kind = PacketKind::Object;
  std::string_view rest = info.substr(11);
  if (parseTimestamp(rest, &p->timestamp)) rest.remove_prefix(7);
  return parsePositionBody(rest, p);
}

bool parseItemInfo(std::string_view info, Packet* p) {
  // )NAME!position / )NAME_position，名字 3-9 字符
  size_t end = std::string_view::npos;
  for (size_t i = 1; i < info.size() && i <= 9; ++i)
    if (info[i] == '!' || info[i] == '_') {
      end = i;
      break;
    }
  if (end == std::string_view::npos || end < 4) return false;
  p->object_name = std::string(trim(info.substr(1, end - 1)));
  p->object_alive = info[end] == '!';
  p->kind = PacketKind::Item;
  return parsePositionBody(info.substr(end + 1), p);
}

bool parseMessageInfo(std::string_view info, Packet* p) {
  if (info.size() < 11 || info[10] != ':') return false;
  p->kind = PacketKind::Message;
  p->message.addressee = std::string(trim(info.substr(1, 9)));
  std::string text(info.substr(11));
  if (text.rfind("ack", 0) == 0 || text.rfind("ACK", 0) == 0) {
    p->message.ack = true;
    p->message.id = std::string(trim(std::string_view(text).substr(3)));
  } else if (text.rfind("rej", 0) == 0 || text.rfind("REJ", 0) == 0) {
    p->message.rej = true;
    p->message.id = std::string(trim(std::string_view(text).substr(3)));
  } else {
    if (text.rfind("BLN", 0) == 0) p->message.bulletin = true;
    if (!text.empty() && text[0] == '}') {
      size_t i = 1;
      int line = 0;
      while (i < text.size() && isDigit(text[i])) {
        line = line * 10 + (text[i] - '0');
        ++i;
      }
      if (line > 0) {
        p->message.line = line;
        if (i < text.size() && text[i] == ':') ++i;
        text = text.substr(i);
      }
    }
    const size_t brace = text.rfind('{');
    if (brace != std::string::npos && brace + 1 < text.size() &&
        text.size() - brace - 1 <= 5) {
      p->message.id = text.substr(brace + 1);
      text.resize(brace);
    }
    p->message.text = std::move(text);
  }
  // 发给本人的消息（由上层按呼号过滤），这里只标记具备消息能力
  p->messaging_capable = true;
  return true;
}

bool parseTelemetryInfo(std::string_view info, Packet* p) {
  if (info.size() < 4 || info[0] != 'T' || info[1] != '#') return false;
  p->kind = PacketKind::Telemetry;
  p->telemetry.present = true;
  const auto parts = split(info.substr(2), ',');
  if (!parts.empty()) p->telemetry.seq = parseInt(parts[0]);
  for (size_t i = 1; i < parts.size() && i <= 5; ++i)
    p->telemetry.analog[i - 1] = std::atof(std::string(parts[i]).c_str());
  if (parts.size() > 6) {
    uint8_t bits = 0;
    const auto dv = parts[6];
    for (size_t i = 0; i < dv.size() && i < 8; ++i)
      if (dv[i] == '1') bits = static_cast<uint8_t>(bits | (1u << (7 - i)));
    p->telemetry.digital = bits;
  }
  return true;
}

}  // namespace

bool parseInfo(std::string_view info, Packet* out, std::string* error) {
  if (out == nullptr) return false;
  info = trim(info);
  if (info.empty()) {
    if (error) *error = "info 为空";
    return false;
  }
  out->dti = info[0];
  out->raw_info = std::string(info);
  out->kind = PacketKind::Unknown;

  switch (info[0]) {
    case '!':
    case '=':
      out->kind = PacketKind::Position;
      out->messaging_capable = info[0] == '=';
      if (!parsePositionBody(info.substr(1), out)) {
        if (error) *error = "位置字段无效";
        return false;
      }
      break;
    case '/':
    case '@': {
      out->kind = PacketKind::Position;
      out->messaging_capable = info[0] == '@';
      std::string_view rest = info.substr(1);
      if (parseTimestamp(rest, &out->timestamp)) rest.remove_prefix(7);
      if (!parsePositionBody(rest, out)) {
        if (error) *error = "带时间戳的位置字段无效";
        return false;
      }
      break;
    }
    case '\'':
    case '`':
      if (!parseMicE(out->dest, info, out)) {
        if (error) *error = "Mic-E 字段无效";
        return false;
      }
      out->kind = PacketKind::Position;
      break;
    case ';':
      if (!parseObjectInfo(info, out)) {
        if (error) *error = "对象字段无效";
        return false;
      }
      break;
    case ')':
      if (!parseItemInfo(info, out)) {
        if (error) *error = "条目标识字段无效";
        return false;
      }
      break;
    case ':':
      if (!parseMessageInfo(info, out)) {
        if (error) *error = "消息字段无效";
        return false;
      }
      break;
    case '>':
      out->kind = PacketKind::Status;
      out->status = std::string(trim(info.substr(1)));
      out->comment = out->status;
      break;
    case '?':
      out->kind = PacketKind::Query;
      out->comment = std::string(trim(info.substr(1)));
      break;
    case 'T':
      if (!parseTelemetryInfo(info, out)) {
        out->kind = PacketKind::Unknown;
        out->comment = std::string(info);
      }
      break;
    case '}':
      out->kind = PacketKind::ThirdParty;
      out->comment = std::string(info.substr(1));
      break;
    default:
      out->comment = std::string(info);
      break;
  }

  if (out->has_position) {
    parseAltitudeFromComment(&out->comment, out);
    parseWeatherComment(out->comment, &out->weather);
    // 天气符号或含气象字段才升级为天气包；对象/条目保留自身类型
    if (out->kind == PacketKind::Position &&
        (out->weather.present || out->symbol.code == '_'))
      out->kind = PacketKind::Weather;
    // 非天气符号的 "088/036" 是航向/速度
    if (out->kind != PacketKind::Weather && out->comment.size() >= 7 &&
        isDigit(out->comment[0]) && isDigit(out->comment[1]) &&
        isDigit(out->comment[2]) && out->comment[3] == '/' &&
        isDigit(out->comment[4]) && isDigit(out->comment[5]) &&
        isDigit(out->comment[6])) {
      out->course_deg = parseInt(std::string_view(out->comment).substr(0, 3));
      out->speed_kt = parseInt(std::string_view(out->comment).substr(4, 3));
    }
  }
  return true;
}

bool parseTnc2(std::string_view line, Packet* out, std::string* error) {
  if (out == nullptr) return false;
  *out = Packet{};
  line = trim(line);
  if (line.empty()) {
    if (error) *error = "空行";
    return false;
  }
  const size_t gt = line.find('>');
  if (gt == std::string_view::npos || gt == 0) {
    if (error) *error = "缺少 SRC>DST";
    return false;
  }
  const size_t colon = line.find(':', gt + 1);
  if (colon == std::string_view::npos) {
    if (error) *error = "缺少 info 分隔冒号";
    return false;
  }
  out->source = std::string(trim(line.substr(0, gt)));
  const std::string_view header = line.substr(gt + 1, colon - gt - 1);
  const auto parts = split(header, ',');
  if (parts.empty() || parts[0].empty()) {
    if (error) *error = "缺少目的地址";
    return false;
  }
  out->dest = std::string(trim(parts[0]));
  if (parts.size() > 1) {
    std::string via;
    for (size_t i = 1; i < parts.size(); ++i) {
      if (!via.empty()) via.push_back(',');
      via.append(parts[i]);
      if (parts[i].size() == 3 && parts[i][0] == 'q') out->qconstruct = std::string(parts[i]);
    }
    out->via = std::move(via);
  }
  out->source = upperCopy(trim(out->source));
  out->dest = upperCopy(trim(out->dest));
  out->raw_line = std::string(line);
  const bool ok = parseInfo(line.substr(colon + 1), out, error);
  if (!ok) out->raw_line = std::string(line);
  return ok;
}

std::optional<Packet> unwrapThirdParty(const Packet& p) {
  if (p.kind != PacketKind::ThirdParty) return std::nullopt;
  std::string_view inner = trim(p.raw_info);
  if (inner.empty() || inner[0] != '}') return std::nullopt;
  inner.remove_prefix(1);
  int depth = 0;
  Packet cur;
  while (!inner.empty() && inner[0] == '}' && depth < 2) {
    inner.remove_prefix(1);
    ++depth;
  }
  if (!parseTnc2(inner, &cur)) return std::nullopt;
  return cur;
}

// ---- 地理 ----
double normalizeBearing(double deg) {
  deg = std::fmod(deg, 360.0);
  if (deg < 0) deg += 360.0;
  return deg;
}

double distanceKm(LatLon a, LatLon b) {
  if (!a.valid() || !b.valid()) return -1.0;
  const double lat1 = a.lat * kPi / 180.0;
  const double lat2 = b.lat * kPi / 180.0;
  const double dlat = (b.lat - a.lat) * kPi / 180.0;
  const double dlon = (b.lon - a.lon) * kPi / 180.0;
  const double h = std::sin(dlat / 2) * std::sin(dlat / 2) +
                   std::cos(lat1) * std::cos(lat2) * std::sin(dlon / 2) * std::sin(dlon / 2);
  return 2.0 * kEarthRadiusKm * std::asin(std::min(1.0, std::sqrt(h)));
}

double bearingDeg(LatLon from, LatLon to) {
  if (!from.valid() || !to.valid()) return -1.0;
  const double lat1 = from.lat * kPi / 180.0;
  const double lat2 = to.lat * kPi / 180.0;
  const double dlon = (to.lon - from.lon) * kPi / 180.0;
  const double y = std::sin(dlon) * std::cos(lat2);
  const double x = std::cos(lat1) * std::sin(lat2) -
                   std::sin(lat1) * std::cos(lat2) * std::cos(dlon);
  return normalizeBearing(std::atan2(y, x) * 180.0 / kPi);
}

bool parseLatitude(std::string_view s, double* out) {
  s = trim(s);
  if (out == nullptr || s.size() < 8) return false;
  const char ns = upper(s[7]);
  if (ns != 'N' && ns != 'S') return false;
  if (s[4] != '.' && s[4] != ' ') return false;
  auto digitAt = [&](size_t i) { return isDigit(s[i]) ? s[i] - '0' : 0; };
  const double v = digitAt(0) * 10 + digitAt(1) +
                   (digitAt(2) * 10 + digitAt(3) + (digitAt(5) * 10 + digitAt(6)) / 100.0) / 60.0;
  if (v > 90.0) return false;
  *out = ns == 'S' ? -v : v;
  return true;
}

bool parseLongitude(std::string_view s, double* out) {
  s = trim(s);
  if (out == nullptr || s.size() < 9) return false;
  const char ew = upper(s[8]);
  if (ew != 'E' && ew != 'W') return false;
  if (s[5] != '.' && s[5] != ' ') return false;
  auto digitAt = [&](size_t i) { return isDigit(s[i]) ? s[i] - '0' : 0; };
  const double v = digitAt(0) * 100 + digitAt(1) * 10 + digitAt(2) +
                   (digitAt(3) * 10 + digitAt(4) + (digitAt(6) * 10 + digitAt(7)) / 100.0) / 60.0;
  if (v > 180.0) return false;
  *out = ew == 'W' ? -v : v;
  return true;
}

std::string formatLatitude(double lat) {
  const char ns = lat < 0 ? 'S' : 'N';
  double v = std::fabs(lat);
  int deg = static_cast<int>(v);
  double minutes = (v - deg) * 60.0;
  int min = static_cast<int>(minutes);
  int hund = static_cast<int>(std::lround((minutes - min) * 100.0));
  if (hund == 100) {
    hund = 0;
    ++min;
  }
  if (min == 60) {
    min = 0;
    ++deg;
  }
  char buf[16];
  std::snprintf(buf, sizeof(buf), "%02d%02d.%02d%c", deg, min, hund, ns);
  return buf;
}

std::string formatLongitude(double lon) {
  const char ew = lon < 0 ? 'W' : 'E';
  double v = std::fabs(lon);
  int deg = static_cast<int>(v);
  double minutes = (v - deg) * 60.0;
  int min = static_cast<int>(minutes);
  int hund = static_cast<int>(std::lround((minutes - min) * 100.0));
  if (hund == 100) {
    hund = 0;
    ++min;
  }
  if (min == 60) {
    min = 0;
    ++deg;
  }
  char buf[16];
  std::snprintf(buf, sizeof(buf), "%03d%02d.%02d%c", deg, min, hund, ew);
  return buf;
}

LatLon maidenheadToLatLon(std::string_view grid, bool* ok) {
  LatLon out;
  std::string g(upperCopy(trim(grid)));
  if (ok) *ok = false;
  if (g.size() < 4 || g.size() > 8 || g.size() % 2 != 0) return out;
  if (g[0] < 'A' || g[0] > 'R' || g[1] < 'A' || g[1] > 'R') return out;
  double lon = (g[0] - 'A') * 20.0 - 180.0;
  double lat = (g[1] - 'A') * 10.0 - 90.0;
  lon += (g[2] - '0') * 2.0;
  lat += (g[3] - '0') * 1.0;
  if (g.size() >= 6) {
    if (g[4] < 'A' || g[4] > 'X' || g[5] < 'A' || g[5] > 'X') return out;
    lon += (g[4] - 'A') * (2.0 / 24.0);
    lat += (g[5] - 'A') * (1.0 / 24.0);
  }
  if (g.size() >= 8) {
    if (!isDigit(g[6]) || !isDigit(g[7])) return out;
    lon += (g[6] - '0') * (2.0 / 240.0);
    lat += (g[7] - '0') * (1.0 / 240.0);
  }
  // 取格子中心
  const double w = g.size() >= 8 ? (2.0 / 240.0) / 2.0 : (g.size() >= 6 ? (2.0 / 24.0) / 2.0 : 1.0);
  const double h = g.size() >= 8 ? (1.0 / 240.0) / 2.0 : (g.size() >= 6 ? (1.0 / 24.0) / 2.0 : 0.5);
  out.lat = lat + h;
  out.lon = lon + w;
  if (ok) *ok = true;
  return out;
}

std::string latLonToMaidenhead(LatLon p) {
  if (!p.valid()) return {};
  double lon = p.lon + 180.0;
  double lat = p.lat + 90.0;
  int lon_a = static_cast<int>(lon / 20.0);
  int lat_a = static_cast<int>(lat / 10.0);
  lon -= lon_a * 20.0;
  lat -= lat_a * 10.0;
  int lon_d = static_cast<int>(lon / 2.0);
  int lat_d = static_cast<int>(lat);
  lon -= lon_d * 2.0;
  lat -= lat_d;
  int lon_s = static_cast<int>(lon * 12.0);
  int lat_s = static_cast<int>(lat * 24.0);
  char buf[16];
  std::snprintf(buf, sizeof(buf), "%c%c%d%d%c%c",
                static_cast<char>('A' + lon_a), static_cast<char>('A' + lat_a),
                lon_d, lat_d, static_cast<char>('A' + lon_s), static_cast<char>('A' + lat_s));
  return buf;
}

// ---- 生成 ----
std::string makePosition(LatLon p, Symbol sym, std::string_view comment,
                         bool compressed, bool messaging) {
  std::string out(1, messaging ? '=' : '!');
  if (!p.valid()) return out + "0/0";
  if (compressed) {
    auto enc = [](double v) {
      char c[4];
      for (int i = 3; i >= 0; --i) {
        c[i] = static_cast<char>(v - std::floor(v / 91.0) * 91.0 + 33);
        v = std::floor(v / 91.0);
      }
      return std::string(c, 4);
    };
    out.push_back(sym.table);
    out += enc(380926.0 * (90.0 - p.lat));
    out += enc(190463.0 * (180.0 + p.lon));
    out.push_back(sym.code);
    out += "   ";
  } else {
    out += formatLatitude(p.lat);
    out.push_back(sym.table);
    out += formatLongitude(p.lon);
    out.push_back(sym.code);
  }
  out.append(comment.substr(0, 40));
  return out;
}

std::string makeObject(std::string_view name, LatLon p, Symbol sym,
                       std::string_view comment, bool alive, int64_t now_ms) {
  const int64_t secs = now_ms / 1000;
  const int64_t day = (secs / 86400) % 31 + 1;
  const int64_t hh = (secs / 3600) % 24;
  const int64_t mm = (secs / 60) % 60;
  char ts[8];
  std::snprintf(ts, sizeof(ts), "%02d%02d%02dz", static_cast<int>(day),
                static_cast<int>(hh), static_cast<int>(mm));
  std::string out = ";";
  std::string n(name.substr(0, 9));
  n.resize(9, ' ');
  out += n;
  out.push_back(alive ? '*' : '_');
  out += ts;
  if (!p.valid()) return out;
  out += formatLatitude(p.lat);
  out.push_back(sym.table);
  out += formatLongitude(p.lon);
  out.push_back(sym.code);
  out.append(comment.substr(0, 40));
  return out;
}

std::string makeStatus(std::string_view text) {
  return ">" + std::string(text.substr(0, 62));
}

std::string makeMessage(std::string_view to, std::string_view text,
                        std::string_view id) {
  std::string addr(upperCopy(to.substr(0, 9)));
  addr.resize(9, ' ');
  std::string body(text.substr(0, 67));
  if (!id.empty()) {
    body += '{';
    body += id.substr(0, 5);
  }
  return ":" + addr + ":" + body;
}

std::string makeAck(std::string_view to, std::string_view id, bool reject) {
  std::string addr(upperCopy(to.substr(0, 9)));
  addr.resize(9, ' ');
  return ":" + addr + ":" + (reject ? "rej" : "ack") + std::string(id.substr(0, 5));
}

std::string makeTnc2(std::string_view source, std::string_view dest,
                     std::string_view path, std::string_view info) {
  std::string out(upperCopy(source));
  out.push_back('>');
  out += upperCopy(dest);
  if (!path.empty()) {
    out.push_back(',');
    out += path;
  }
  out.push_back(':');
  out += info;
  return out;
}

uint16_t passcode(std::string_view callsign) {
  // 公开 APRS-IS 算法：0x73e2 种子；偶数位左移 8、奇数位直接异或；剥 SSID。
  uint32_t h = 0x73e2;
  size_t n = 0;
  for (char c : callsign) {
    if (c == '-' || c == '\0') break;
    const auto u = static_cast<uint8_t>(std::toupper(static_cast<unsigned char>(c)));
    h ^= static_cast<uint32_t>(u) << ((n % 2 == 0) ? 8 : 0);
    ++n;
  }
  return static_cast<uint16_t>(h & 0x7fff);
}

// ---- 过滤器 ----
namespace {
char filterChar(FilterKind k) {
  switch (k) {
    case FilterKind::Range: return 'r';
    case FilterKind::MyRange: return 'm';
    case FilterKind::Buddy: return 'b';
    case FilterKind::Prefix: return 'p';
    case FilterKind::Object: return 'o';
    case FilterKind::Type: return 't';
    case FilterKind::Group: return 'g';
    case FilterKind::Unproto: return 'u';
    case FilterKind::Area: return 'a';
    case FilterKind::Entry: return 'e';
    case FilterKind::Friend: return 'f';
    case FilterKind::Digipeater: return 'd';
    case FilterKind::QConstruct: return 'q';
  }
  return 'r';
}

bool validCall(std::string_view s) {
  if (s.empty() || s.size() > 9) return false;
  for (char c : s) {
    const char u = upper(c);
    if (!(u >= 'A' && u <= 'Z') && !isDigit(u) && u != '-') return false;
  }
  return true;
}

bool parseDouble(std::string_view s, double* out) {
  std::string tmp(s);
  char* end = nullptr;
  const double v = std::strtod(tmp.c_str(), &end);
  if (end == tmp.c_str() || *end != '\0') return false;
  *out = v;
  return true;
}

bool parseAllCalls(const std::vector<std::string_view>& vals, std::string* arg,
                   std::string* error) {
  std::string out;
  for (auto v : vals) {
    if (!validCall(v)) {
      if (error) *error = "呼号不合法：" + std::string(v);
      return false;
    }
    if (!out.empty()) out.push_back('/');
    out += upperCopy(v);
  }
  *arg = out;
  return !out.empty();
}
}  // namespace

bool parseFilterLine(std::string_view text, FilterLine* out,
                     std::string* error) {
  if (out == nullptr) return false;
  text = trim(text);
  if (text.size() < 3 || text[1] != '/') {
    if (error) *error = "过滤器格式应为 x/参数";
    return false;
  }
  FilterLine f;
  const auto vals = split(text.substr(2), '/');
  switch (lower(text[0])) {
    case 'r': {
      if (vals.size() != 3) {
        if (error) *error = "范围过滤器应为 r/纬度/经度/公里";
        return false;
      }
      double lat = 0, lon = 0, km = 0;
      if (!parseDouble(vals[0], &lat) || lat < -90 || lat > 90 ||
          !parseDouble(vals[1], &lon) || lon < -180 || lon > 180 ||
          !parseDouble(vals[2], &km) || km <= 0 || km > 50000) {
        if (error) *error = "范围过滤器参数越界";
        return false;
      }
      f.kind = FilterKind::Range;
      char buf[64];
      std::snprintf(buf, sizeof(buf), "%.4f/%.4f/%.0f", lat, lon, km);
      f.arg = buf;
      break;
    }
    case 'm': {
      if (vals.size() != 1) {
        if (error) *error = "我的范围过滤器应为 m/公里";
        return false;
      }
      double km = 0;
      if (!parseDouble(vals[0], &km) || km <= 0 || km > 50000) {
        if (error) *error = "m/ 公里数越界";
        return false;
      }
      f.kind = FilterKind::MyRange;
      f.arg = std::to_string(static_cast<int>(km));
      break;
    }
    case 'b':
    case 'g':
    case 'e':
    case 'd':
      f.kind = lower(text[0]) == 'b'   ? FilterKind::Buddy
               : lower(text[0]) == 'g' ? FilterKind::Group
               : lower(text[0]) == 'e' ? FilterKind::Entry
                                        : FilterKind::Digipeater;
      if (!parseAllCalls(vals, &f.arg, error)) return false;
      break;
    case 'p': {
      f.kind = FilterKind::Prefix;
      std::string outarg;
      for (auto v : vals) {
        if (v.empty() || v.size() > 8) {
          if (error) *error = "前缀不合法";
          return false;
        }
        for (char c : v)
          if (!std::isalnum(static_cast<unsigned char>(c))) {
            if (error) *error = "前缀只能含字母数字";
            return false;
          }
        if (!outarg.empty()) outarg.push_back('/');
        outarg += upperCopy(v);
      }
      f.arg = outarg;
      break;
    }
    case 'o': {
      f.kind = FilterKind::Object;
      std::string outarg;
      for (auto v : vals) {
        if (v.empty() || v.size() > 9) {
          if (error) *error = "对象名不合法";
          return false;
        }
        if (!outarg.empty()) outarg.push_back('/');
        outarg += std::string(v);
      }
      f.arg = outarg;
      break;
    }
    case 't': {
      f.kind = FilterKind::Type;
      std::string outarg;
      for (auto v : vals) {
        for (char c : v) {
          if (std::string_view("poimqstunw").find(lower(c)) == std::string_view::npos) {
            if (error) *error = "未知包类型字符（可用 poimqstunw）";
            return false;
          }
          if (outarg.find(lower(c)) == std::string::npos) outarg.push_back(lower(c));
        }
      }
      if (outarg.empty()) {
        if (error) *error = "t/ 至少一个类型字符";
        return false;
      }
      f.arg = outarg;
      break;
    }
    case 'u': {
      f.kind = FilterKind::Unproto;
      std::string outarg;
      for (auto v : vals) {
        if (v.empty() || v.size() > 9 || !validCall(v)) {
          if (error) *error = "未协议目的地址不合法";
          return false;
        }
        if (!outarg.empty()) outarg.push_back('/');
        outarg += upperCopy(v);
      }
      f.arg = outarg;
      break;
    }
    case 'a': {
      if (vals.size() != 4) {
        if (error) *error = "区域过滤器应为 a/北纬/西经/南纬/东经";
        return false;
      }
      double n = 0, w = 0, s = 0, e = 0;
      if (!parseDouble(vals[0], &n) || n < -90 || n > 90 ||
          !parseDouble(vals[1], &w) || w < -180 || w > 180 ||
          !parseDouble(vals[2], &s) || s < -90 || s > 90 ||
          !parseDouble(vals[3], &e) || e < -180 || e > 180 || n <= s || e <= w) {
        if (error) *error = "区域边界无效（北>南、东>西）";
        return false;
      }
      f.kind = FilterKind::Area;
      char buf[80];
      std::snprintf(buf, sizeof(buf), "%.4f/%.4f/%.4f/%.4f", n, w, s, e);
      f.arg = buf;
      break;
    }
    case 'f': {
      if (vals.size() != 2) {
        if (error) *error = "友台过滤器应为 f/呼号/公里";
        return false;
      }
      if (!validCall(vals[0])) {
        if (error) *error = "呼号不合法";
        return false;
      }
      double km = 0;
      if (!parseDouble(vals[1], &km) || km <= 0 || km > 50000) {
        if (error) *error = "f/ 公里数越界";
        return false;
      }
      f.kind = FilterKind::Friend;
      f.arg = upperCopy(vals[0]) + "/" + std::to_string(static_cast<int>(km));
      break;
    }
    case 'q': {
      f.kind = FilterKind::QConstruct;
      std::string outarg;
      for (auto v : vals) {
        // APRS-IS 允许 q/qAR,qAI 逗号连写
        for (auto part : split(v, ',')) {
          if (part.size() < 3 || part.size() > 4 || upper(part[0]) != 'Q') {
            if (error) *error = "q 构造应为 qXX";
            return false;
          }
          if (!outarg.empty()) outarg.push_back(',');
          outarg += upperCopy(part);
        }
      }
      f.arg = outarg;
      break;
    }
    default:
      if (error) *error = "未知过滤器类型（可用 r m b p o t g u a e f d q）";
      return false;
  }
  *out = std::move(f);
  return true;
}

std::string filterToString(const FilterLine& f) {
  return std::string(1, filterChar(f.kind)) + "/" + f.arg;
}

std::string buildFilter(const std::vector<FilterLine>& filters) {
  std::string out;
  for (const auto& f : filters) {
    if (f.arg.empty()) continue;
    if (!out.empty()) out.push_back(' ');
    out += filterToString(f);
  }
  return out;
}

// ---- AX.25 / KISS ----
namespace {
std::string addrCall(const Ax25Address& a) {
  std::string out = a.call;
  if (a.ssid != 0) out += "-" + std::to_string(a.ssid);
  return out;
}

void writeAddr(std::vector<uint8_t>* out, const Ax25Address& a) {
  std::string call(a.call.substr(0, 6));
  call.resize(6, ' ');
  for (char c : call) out->push_back(static_cast<uint8_t>(c) << 1);
  uint8_t ssid = static_cast<uint8_t>(0x60 | ((a.ssid & 0x0F) << 1));
  if (a.last) ssid |= 0x01;
  if (a.repeated) ssid |= 0x80;
  out->push_back(ssid);
}
}  // namespace

std::vector<uint8_t> encodeAx25(const Ax25Frame& f) {
  std::vector<uint8_t> out;
  Ax25Address dest = f.dest;
  dest.last = false;
  writeAddr(&out, dest);
  Ax25Address src = f.source;
  src.last = f.via.empty();
  writeAddr(&out, src);
  for (size_t i = 0; i < f.via.size(); ++i) {
    Ax25Address a = f.via[i];
    a.last = i + 1 == f.via.size();
    writeAddr(&out, a);
  }
  out.push_back(0x03);  // UI
  out.push_back(0xF0);  // no layer 3
  out.insert(out.end(), f.info.begin(), f.info.end());
  return out;
}

bool parseAx25(std::span<const uint8_t> bytes, Ax25Frame* out,
               std::string* error) {
  if (out == nullptr) return false;
  if (bytes.size() < 16) {
    if (error) *error = "AX.25 帧过短";
    return false;
  }
  std::vector<Ax25Address> addrs;
  size_t pos = 0;
  while (pos + 7 <= bytes.size() && addrs.size() < 10) {
    std::string call;
    for (size_t i = 0; i < 6; ++i) {
      const char c = static_cast<char>(bytes[pos + i] >> 1);
      if (c != ' ') call.push_back(c);
    }
    Ax25Address a;
    a.call = call;
    const uint8_t b = bytes[pos + 6];
    a.ssid = static_cast<uint8_t>((b >> 1) & 0x0F);
    a.repeated = (b & 0x80) != 0;
    a.last = (b & 0x01) != 0;
    addrs.push_back(std::move(a));
    pos += 7;
    if (addrs.back().last) break;
  }
  if (addrs.size() < 2) {
    if (error) *error = "地址字段不足";
    return false;
  }
  if (!addrs.back().last) {
    if (error) *error = "AX.25 地址未结束";
    return false;
  }
  if (pos + 2 > bytes.size()) {
    if (error) *error = "缺少控制/PID";
    return false;
  }
  const uint8_t ctrl = bytes[pos++];
  if ((ctrl & 0xEF) != 0x03) {
    if (error) *error = "非 UI 帧";
    return false;
  }
  if (bytes[pos] != 0xF0) {
    if (error) *error = "PID 不是 0xF0";
    return false;
  }
  ++pos;
  out->dest = addrs[0];
  out->source = addrs[1];
  out->via.assign(addrs.begin() + 2, addrs.end());
  out->info.assign(reinterpret_cast<const char*>(bytes.data() + pos),
                   bytes.size() - pos);
  return true;
}

std::string ax25ToTnc2(const Ax25Frame& f) {
  std::string out = addrCall(f.source);
  out.push_back('>');
  out += addrCall(f.dest);
  for (const auto& a : f.via) {
    out.push_back(',');
    out += addrCall(a);
    if (a.repeated) out.push_back('*');
  }
  out.push_back(':');
  out += f.info;
  return out;
}

std::optional<Ax25Frame> tnc2ToAx25(const Packet& p, bool dest_is_lat) {
  if (dest_is_lat) return std::nullopt;  // Mic-E 发射需专用编码，暂不支持
  Ax25Frame f;
  auto set = [](Ax25Address* a, std::string_view s) {
    const size_t dash = s.rfind('-');
    if (dash != std::string_view::npos) {
      a->call = upperCopy(s.substr(0, dash));
      a->ssid = static_cast<uint8_t>(parseInt(s.substr(dash + 1)) & 0x0F);
    } else {
      a->call = upperCopy(s);
    }
  };
  set(&f.source, p.source);
  set(&f.dest, p.dest);
  if (!p.via.empty()) {
    for (auto v : split(p.via, ',')) {
      std::string_view s = trim(v);
      if (s.empty()) continue;
      if (s[0] == 'q' && s.size() <= 4) continue;  // q 构造只属于 APRS-IS
      if (!s.empty() && s.back() == '*') s.remove_suffix(1);
      Ax25Address a;
      set(&a, s);
      if (!a.call.empty()) f.via.push_back(std::move(a));
    }
  }
  f.info = p.raw_info;
  return f;
}

std::vector<uint8_t> kissEncode(std::span<const uint8_t> payload, uint8_t port) {
  std::vector<uint8_t> out;
  out.reserve(payload.size() + 4);
  out.push_back(0xC0);
  auto pushEsc = [&out](uint8_t b) {
    if (b == 0xC0) {
      out.push_back(0xDB);
      out.push_back(0xDC);
    } else if (b == 0xDB) {
      out.push_back(0xDB);
      out.push_back(0xDD);
    } else {
      out.push_back(b);
    }
  };
  pushEsc(static_cast<uint8_t>(port & 0x0F));
  for (uint8_t b : payload) pushEsc(b);
  out.push_back(0xC0);
  return out;
}

void KissDecoder::feed(std::span<const uint8_t> bytes) {
  for (uint8_t b : bytes) {
    if (b == 0xC0) {
      if (in_frame_ && !buf_.empty()) {
        if (buf_.size() >= 2 && buf_.size() <= static_cast<size_t>(kMaxAx25Bytes) + 2) {
          KissFrame f;
          f.port = static_cast<uint8_t>(buf_[0] & 0x0F);
          f.data.assign(buf_.begin() + 1, buf_.end());
          if (!f.data.empty()) ready_.push_back(std::move(f));
        } else {
          ++dropped_;
        }
      }
      in_frame_ = true;
      escape_ = false;
      overflow_ = false;
      buf_.clear();
      continue;
    }
    if (!in_frame_) continue;
    if (overflow_) continue;
    if (b == 0xDB) {
      escape_ = true;
      continue;
    }
    if (escape_) {
      if (b == 0xDC) b = 0xC0;
      else if (b == 0xDD) b = 0xDB;
      else {
        // 非法转义：丢弃本帧
        overflow_ = true;
        ++dropped_;
        continue;
      }
      escape_ = false;
    }
    buf_.push_back(b);
    if (buf_.size() > static_cast<size_t>(kMaxAx25Bytes) + 2) {
      overflow_ = true;
      ++dropped_;
    }
  }
}

bool KissDecoder::pop(KissFrame* out) {
  if (ready_.empty()) return false;
  if (out) *out = std::move(ready_.front());
  ready_.erase(ready_.begin());
  return true;
}

void KissDecoder::reset() {
  in_frame_ = false;
  escape_ = false;
  overflow_ = false;
  buf_.clear();
  ready_.clear();
}

}  // namespace radio::aprs
