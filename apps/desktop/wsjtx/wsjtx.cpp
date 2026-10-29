// SPDX-License-Identifier: AGPL-3.0-or-later
#include "wsjtx/wsjtx.h"

#include <cstring>
#include <type_traits>

namespace radio::wsjtx {
namespace {

constexpr int64_t kJulianEpoch = 2440588;  // 1970-01-01
constexpr int64_t kDayMs = 86'400'000;

std::vector<uint16_t> utf16FromUtf8(std::string_view s) {
  std::vector<uint16_t> out;
  out.reserve(s.size());
  size_t i = 0;
  while (i < s.size()) {
    const auto b0 = static_cast<uint8_t>(s[i]);
    uint32_t cp = 0xFFFD;
    size_t len = 1;
    if (b0 < 0x80) {
      cp = b0;
    } else if ((b0 & 0xE0) == 0xC0) {
      len = 2;
      cp = b0 & 0x1F;
    } else if ((b0 & 0xF0) == 0xE0) {
      len = 3;
      cp = b0 & 0x0F;
    } else if ((b0 & 0xF8) == 0xF0) {
      len = 4;
      cp = b0 & 0x07;
    }
    bool valid = (len == 1) || i + len <= s.size();
    for (size_t k = 1; valid && k < len; ++k) {
      const auto b = static_cast<uint8_t>(s[i + k]);
      if ((b & 0xC0) != 0x80) {
        valid = false;
      } else {
        cp = (cp << 6) | (b & 0x3F);
      }
    }
    if (!valid) {
      cp = 0xFFFD;
      len = 1;
    } else if ((len == 2 && cp < 0x80) || (len == 3 && cp < 0x800) ||
               (len == 4 && cp < 0x10000) || cp > 0x10FFFF) {
      cp = 0xFFFD;
    }
    i += len;
    if (cp < 0x10000) {
      out.push_back(static_cast<uint16_t>(cp));
    } else {
      cp -= 0x10000;
      out.push_back(static_cast<uint16_t>(0xD800 + (cp >> 10)));
      out.push_back(static_cast<uint16_t>(0xDC00 + (cp & 0x3FF)));
    }
  }
  return out;
}

std::string utf8FromUtf16(const std::vector<uint16_t>& u) {
  std::string out;
  out.reserve(u.size());
  for (size_t i = 0; i < u.size(); ++i) {
    uint32_t cp = u[i];
    if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < u.size() && u[i + 1] >= 0xDC00 &&
        u[i + 1] <= 0xDFFF) {
      cp = 0x10000 + ((cp - 0xD800) << 10) + (u[i + 1] - 0xDC00);
      ++i;
    } else if (cp >= 0xD800 && cp <= 0xDFFF) {
      cp = 0xFFFD;
    }
    if (cp < 0x80) {
      out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
      out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
      out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
      out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
      out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
      out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
      out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
      out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
      out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
      out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
  }
  return out;
}

class Writer {
 public:
  void u8(uint8_t v) { buf_.push_back(v); }

  void u32(uint32_t v) {
    for (int shift = 24; shift >= 0; shift -= 8)
      buf_.push_back(static_cast<uint8_t>(v >> shift));
  }

  void u64(uint64_t v) {
    for (int shift = 56; shift >= 0; shift -= 8)
      buf_.push_back(static_cast<uint8_t>(v >> shift));
  }

  void i32(int32_t v) { u32(static_cast<uint32_t>(v)); }

  void f64(double v) {
    uint64_t bits = 0;
    static_assert(sizeof(bits) == sizeof(v));
    std::memcpy(&bits, &v, sizeof(bits));
    u64(bits);
  }

  void boolean(bool v) { u8(v ? 1 : 0); }

  void str(std::string_view s) {
    const auto units = utf16FromUtf8(s);
    u32(static_cast<uint32_t>(units.size()));
    for (uint16_t unit : units) {
      u8(static_cast<uint8_t>(unit >> 8));
      u8(static_cast<uint8_t>(unit & 0xFF));
    }
  }

  void dateTime(const DateTime& dt) {
    u64(static_cast<uint64_t>(dt.julian_day));
    u32(dt.msecs);
    u8(dt.spec);
    if (dt.spec == 2) i32(dt.offset_sec);
  }

  std::vector<uint8_t> take() { return std::move(buf_); }

 private:
  std::vector<uint8_t> buf_;
};

class Reader {
 public:
  explicit Reader(std::span<const uint8_t> bytes) : bytes_(bytes) {}

  bool ok() const { return ok_; }
  size_t remaining() const { return ok_ ? bytes_.size() - pos_ : 0; }

  uint8_t u8() {
    if (!need(1)) return 0;
    return bytes_[pos_++];
  }

  uint32_t u32() {
    if (!need(4)) return 0;
    uint32_t v = 0;
    for (int i = 0; i < 4; ++i) v = (v << 8) | bytes_[pos_++];
    return v;
  }

  uint64_t u64() {
    if (!need(8)) return 0;
    uint64_t v = 0;
    for (int i = 0; i < 8; ++i) v = (v << 8) | bytes_[pos_++];
    return v;
  }

  int32_t i32() { return static_cast<int32_t>(u32()); }

  double f64() {
    const uint64_t bits = u64();
    double v = 0.0;
    std::memcpy(&v, &bits, sizeof(v));
    return v;
  }

  bool boolean() { return u8() != 0; }

  std::string str() {
    const uint32_t units = u32();
    if (!ok_ || units == 0xFFFFFFFFu) return {};
    if (units > remaining() / 2) {
      ok_ = false;
      return {};
    }
    std::vector<uint16_t> u;
    u.reserve(units);
    for (uint32_t i = 0; i < units; ++i) {
      const uint16_t hi = u8();
      const uint16_t lo = u8();
      if (!ok_) return {};
      u.push_back(static_cast<uint16_t>((hi << 8) | lo));
    }
    return utf8FromUtf16(u);
  }

  DateTime dateTime() {
    DateTime dt;
    dt.julian_day = static_cast<int64_t>(u64());
    dt.msecs = u32();
    dt.spec = u8();
    if (dt.spec == 2) dt.offset_sec = i32();
    if (dt.spec > 2) ok_ = false;
    return dt;
  }

  void skip(size_t n) {
    if (need(n)) pos_ += n;
  }

 private:
  bool need(size_t n) {
    if (!ok_ || bytes_.size() - pos_ < n) {
      ok_ = false;
      return false;
    }
    return true;
  }

  std::span<const uint8_t> bytes_;
  size_t pos_ = 0;
  bool ok_ = true;
};

void writeHeader(Writer& w, MessageType type, std::string_view id) {
  w.u32(kMagic);
  w.u32(kSchema);
  w.u32(static_cast<uint32_t>(type));
  w.str(id);
}

template <typename T>
void writeBody(Writer& w, MessageType type, const T& body);

template <>
void writeBody<Heartbeat>(Writer& w, MessageType, const Heartbeat& b) {
  w.u32(b.max_schema);
  w.str(b.version);
  w.str(b.revision);
}

template <>
void writeBody<Status>(Writer& w, MessageType, const Status& s) {
  w.u64(s.dial_frequency);
  w.str(s.mode);
  w.str(s.dx_call);
  w.str(s.report);
  w.str(s.tx_mode);
  w.boolean(s.tx_enabled);
  w.boolean(s.transmitting);
  w.boolean(s.decoding);
  w.u32(s.rx_df);
  w.u32(s.tx_df);
  w.str(s.de_call);
  w.str(s.de_grid);
  w.str(s.dx_grid);
  w.boolean(s.tx_watchdog);
  w.str(s.sub_mode);
  w.boolean(s.fast_mode);
  w.u8(s.special_operation_mode);
  w.u32(s.frequency_tolerance);
  w.u32(s.tr_period);
  w.str(s.configuration_name);
  w.str(s.tx_message);
}

template <>
void writeBody<Decode>(Writer& w, MessageType, const Decode& d) {
  w.boolean(d.is_new);
  w.u32(d.time_ms);
  w.i32(d.snr);
  w.f64(d.delta_time);
  w.i32(d.delta_frequency);
  w.str(d.mode);
  w.str(d.message);
  w.boolean(d.low_confidence);
  w.boolean(d.off_air);
}

template <>
void writeBody<Clear>(Writer& w, MessageType, const Clear& c) {
  w.u8(c.window);
}

template <>
void writeBody<Reply>(Writer& w, MessageType, const Reply& r) {
  w.u32(r.time_ms);
  w.i32(r.snr);
  w.f64(r.delta_time);
  w.i32(r.delta_frequency);
  w.str(r.mode);
  w.str(r.message);
  w.boolean(r.low_confidence);
  w.u8(r.modifiers);
}

template <>
void writeBody<QsoLogged>(Writer& w, MessageType, const QsoLogged& q) {
  w.dateTime(q.date_time_off);
  w.str(q.dx_call);
  w.str(q.dx_grid);
  w.u64(q.tx_frequency);
  w.str(q.mode);
  w.str(q.report_sent);
  w.str(q.report_received);
  w.str(q.tx_power);
  w.str(q.comments);
  w.str(q.name);
  w.dateTime(q.date_time_on);
  w.str(q.operator_call);
  w.str(q.my_call);
  w.str(q.my_grid);
  w.str(q.exchange_sent);
  w.str(q.exchange_received);
  w.str(q.adif_propagation);
}

template <>
void writeBody<Close>(Writer&, MessageType, const Close&) {}
template <>
void writeBody<Replay>(Writer&, MessageType, const Replay&) {}

template <>
void writeBody<HaltTx>(Writer& w, MessageType, const HaltTx& h) {
  w.boolean(h.auto_tx_only);
}

template <>
void writeBody<FreeText>(Writer& w, MessageType, const FreeText& f) {
  w.str(f.text);
  w.boolean(f.send);
}

template <>
void writeBody<WsprDecode>(Writer& w, MessageType, const WsprDecode& d) {
  w.boolean(d.is_new);
  w.u32(d.time_ms);
  w.i32(d.snr);
  w.f64(d.delta_time);
  w.u64(d.frequency);
  w.i32(d.drift);
  w.str(d.callsign);
  w.str(d.grid);
  w.i32(d.power_dbm);
  w.boolean(d.off_air);
}

template <>
void writeBody<Location>(Writer& w, MessageType, const Location& l) {
  w.str(l.grid);
}

template <>
void writeBody<LoggedAdif>(Writer& w, MessageType, const LoggedAdif& a) {
  w.str(a.adif);
}

template <>
void writeBody<SwitchConfiguration>(Writer& w, MessageType,
                                    const SwitchConfiguration& s) {
  w.str(s.name);
}

template <>
void writeBody<Configure>(Writer& w, MessageType, const Configure& c) {
  w.str(c.mode);
  w.u32(c.frequency_tolerance);
  w.str(c.sub_mode);
  w.boolean(c.fast_mode);
  w.u32(c.tr_period);
  w.u32(c.rx_df);
  w.str(c.dx_call);
  w.str(c.dx_grid);
  w.boolean(c.generate_messages);
}

bool readBody(Reader& r, MessageType type, uint32_t schema, Body* out) {
  switch (type) {
    case MessageType::Heartbeat: {
      Heartbeat h;
      if (schema >= 3) h.max_schema = r.u32();
      else h.max_schema = 2;
      h.version = r.str();
      h.revision = r.str();
      *out = std::move(h);
      break;
    }
    case MessageType::Status: {
      Status s;
      s.dial_frequency = r.u64();
      s.mode = r.str();
      s.dx_call = r.str();
      s.report = r.str();
      s.tx_mode = r.str();
      s.tx_enabled = r.boolean();
      s.transmitting = r.boolean();
      s.decoding = r.boolean();
      s.rx_df = r.u32();
      s.tx_df = r.u32();
      s.de_call = r.str();
      s.de_grid = r.str();
      s.dx_grid = r.str();
      s.tx_watchdog = r.boolean();
      s.sub_mode = r.str();
      s.fast_mode = r.boolean();
      s.special_operation_mode = r.u8();
      s.frequency_tolerance = r.u32();
      s.tr_period = r.u32();
      s.configuration_name = r.str();
      s.tx_message = r.str();
      *out = std::move(s);
      break;
    }
    case MessageType::Decode: {
      Decode d;
      d.is_new = r.boolean();
      d.time_ms = r.u32();
      d.snr = r.i32();
      d.delta_time = r.f64();
      d.delta_frequency = r.i32();
      d.mode = r.str();
      d.message = r.str();
      d.low_confidence = r.boolean();
      d.off_air = r.boolean();
      *out = std::move(d);
      break;
    }
    case MessageType::Clear: {
      Clear c;
      c.window = r.u8();
      *out = c;
      break;
    }
    case MessageType::Reply: {
      Reply p;
      p.time_ms = r.u32();
      p.snr = r.i32();
      p.delta_time = r.f64();
      p.delta_frequency = r.i32();
      p.mode = r.str();
      p.message = r.str();
      p.low_confidence = r.boolean();
      p.modifiers = r.u8();
      *out = std::move(p);
      break;
    }
    case MessageType::QsoLogged: {
      QsoLogged q;
      q.date_time_off = r.dateTime();
      q.dx_call = r.str();
      q.dx_grid = r.str();
      q.tx_frequency = r.u64();
      q.mode = r.str();
      q.report_sent = r.str();
      q.report_received = r.str();
      q.tx_power = r.str();
      q.comments = r.str();
      q.name = r.str();
      q.date_time_on = r.dateTime();
      q.operator_call = r.str();
      q.my_call = r.str();
      q.my_grid = r.str();
      q.exchange_sent = r.str();
      q.exchange_received = r.str();
      q.adif_propagation = r.str();
      *out = std::move(q);
      break;
    }
    case MessageType::Close:
      *out = Close{};
      break;
    case MessageType::Replay:
      *out = Replay{};
      break;
    case MessageType::HaltTx: {
      HaltTx h;
      h.auto_tx_only = r.boolean();
      *out = h;
      break;
    }
    case MessageType::FreeText: {
      FreeText f;
      f.text = r.str();
      f.send = r.boolean();
      *out = std::move(f);
      break;
    }
    case MessageType::WsprDecode: {
      WsprDecode d;
      d.is_new = r.boolean();
      d.time_ms = r.u32();
      d.snr = r.i32();
      d.delta_time = r.f64();
      d.frequency = r.u64();
      d.drift = r.i32();
      d.callsign = r.str();
      d.grid = r.str();
      d.power_dbm = r.i32();
      d.off_air = r.boolean();
      *out = std::move(d);
      break;
    }
    case MessageType::Location: {
      Location l;
      l.grid = r.str();
      *out = std::move(l);
      break;
    }
    case MessageType::LoggedAdif: {
      LoggedAdif a;
      a.adif = r.str();
      *out = std::move(a);
      break;
    }
    case MessageType::SwitchConfiguration: {
      SwitchConfiguration s;
      s.name = r.str();
      *out = std::move(s);
      break;
    }
    case MessageType::Configure: {
      Configure c;
      c.mode = r.str();
      c.frequency_tolerance = r.u32();
      c.sub_mode = r.str();
      c.fast_mode = r.boolean();
      c.tr_period = r.u32();
      c.rx_df = r.u32();
      c.dx_call = r.str();
      c.dx_grid = r.str();
      c.generate_messages = r.boolean();
      *out = std::move(c);
      break;
    }
    case MessageType::HighlightCallsign:
    case MessageType::AnnotationInfo:
      return false;
    default:
      return false;
  }
  return r.ok();
}

std::vector<uint8_t> encodeImpl(const Message& message) {
  Writer w;
  writeHeader(w, message.type, message.id);
  std::visit([&](const auto& body) {
    writeBody<std::decay_t<decltype(body)>>(w, message.type, body);
  }, message.body);
  return w.take();
}

}  // namespace

const char* messageTypeName(MessageType type) {
  switch (type) {
    case MessageType::Heartbeat: return "Heartbeat";
    case MessageType::Status: return "Status";
    case MessageType::Decode: return "Decode";
    case MessageType::Clear: return "Clear";
    case MessageType::Reply: return "Reply";
    case MessageType::QsoLogged: return "QSO Logged";
    case MessageType::Close: return "Close";
    case MessageType::Replay: return "Replay";
    case MessageType::HaltTx: return "Halt Tx";
    case MessageType::FreeText: return "Free Text";
    case MessageType::WsprDecode: return "WSPR Decode";
    case MessageType::Location: return "Location";
    case MessageType::LoggedAdif: return "Logged ADIF";
    case MessageType::HighlightCallsign: return "Highlight Callsign";
    case MessageType::SwitchConfiguration: return "Switch Configuration";
    case MessageType::Configure: return "Configure";
    case MessageType::AnnotationInfo: return "Annotation Info";
  }
  return "Unknown";
}

int64_t DateTime::utcMs() const {
  int64_t ms = (julian_day - kJulianEpoch) * kDayMs + static_cast<int64_t>(msecs);
  if (spec == 2) ms -= static_cast<int64_t>(offset_sec) * 1000;
  return ms;
}

DateTime DateTime::fromUtcMs(int64_t ms) {
  DateTime dt;
  int64_t days = ms / kDayMs;
  int64_t rem = ms % kDayMs;
  if (rem < 0) {
    rem += kDayMs;
    --days;
  }
  dt.julian_day = kJulianEpoch + days;
  dt.msecs = static_cast<uint32_t>(rem);
  dt.spec = 1;  // UTC
  dt.offset_sec = 0;
  return dt;
}

std::vector<uint8_t> encode(const Message& message) {
  return encodeImpl(message);
}

bool decode(std::span<const uint8_t> bytes, Message* out, std::string* error) {
  Reader r(bytes);
  const uint32_t magic = r.u32();
  if (!r.ok() || magic != kMagic) {
    if (error) *error = "bad magic";
    return false;
  }
  const uint32_t schema = r.u32();
  if (!r.ok() || schema == 0) {
    if (error) *error = "bad schema";
    return false;
  }
  const uint32_t raw_type = r.u32();
  if (!r.ok()) {
    if (error) *error = "truncated header";
    return false;
  }
  if (raw_type > static_cast<uint32_t>(MessageType::AnnotationInfo)) {
    if (error) *error = "unknown message type " + std::to_string(raw_type);
    return false;
  }
  const auto type = static_cast<MessageType>(raw_type);
  std::string id = r.str();
  if (!r.ok()) {
    if (error) *error = "truncated id";
    return false;
  }
  Body body = Heartbeat{};
  if (!readBody(r, type, schema, &body)) {
    if (error) *error = "unsupported or truncated body for " +
                        std::string(messageTypeName(type));
    return false;
  }
  if (out) {
    out->type = type;
    out->id = std::move(id);
    out->body = std::move(body);
  }
  return true;
}

Message makeHeartbeat(std::string id, std::string version, std::string revision) {
  return Message{MessageType::Heartbeat, std::move(id),
                 Heartbeat{kSchema, std::move(version), std::move(revision)}};
}

Message makeReply(std::string id, const Decode& decode, uint8_t modifiers) {
  Reply reply;
  reply.time_ms = decode.time_ms;
  reply.snr = decode.snr;
  reply.delta_time = decode.delta_time;
  reply.delta_frequency = decode.delta_frequency;
  reply.mode = decode.mode;
  reply.message = decode.message;
  reply.low_confidence = decode.low_confidence;
  reply.modifiers = modifiers;
  return Message{MessageType::Reply, std::move(id), std::move(reply)};
}

Message makeHaltTx(std::string id, bool auto_tx_only) {
  return Message{MessageType::HaltTx, std::move(id), HaltTx{auto_tx_only}};
}

Message makeFreeText(std::string id, std::string text, bool send) {
  return Message{MessageType::FreeText, std::move(id),
                 FreeText{std::move(text), send}};
}

Message makeReplay(std::string id) {
  return Message{MessageType::Replay, std::move(id), Replay{}};
}

Message makeLocation(std::string id, std::string grid) {
  return Message{MessageType::Location, std::move(id), Location{std::move(grid)}};
}

Message makeClear(std::string id, uint8_t window) {
  return Message{MessageType::Clear, std::move(id), Clear{window}};
}

Message makeClose(std::string id) {
  return Message{MessageType::Close, std::move(id), Close{}};
}

}  // namespace radio::wsjtx
