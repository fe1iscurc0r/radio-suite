// SPDX-License-Identifier: AGPL-3.0-or-later
#include "core/civ.h"

#include <QMap>

#include <cctype>
#include <cmath>

namespace radio::core::civ {
namespace {

uint8_t bcdByte(int v) {
  return static_cast<uint8_t>(((v / 10) % 10) << 4 | (v % 10));
}

// 定宽 BCD，MSB-first（channel/level/菜单值用；与 0x03/0x05 的频率 LSB-first 相反）
std::vector<uint8_t> bcdMsb(int value, int bytes) {
  std::vector<uint8_t> out(static_cast<size_t>(bytes), 0);
  for (int i = bytes - 1; i >= 0; --i) {
    out[static_cast<size_t>(i)] = bcdByte(value % 100);
    value /= 100;
  }
  return out;
}

std::optional<int> bcdMsbToInt(std::span<const uint8_t> data) {
  int v = 0;
  for (uint8_t b : data) {
    const int hi = b >> 4, lo = b & 0x0F;
    if (hi > 9 || lo > 9) return std::nullopt;
    v = v * 100 + hi * 10 + lo;
  }
  return v;
}

int roundHalfEven(int num, int den) {
  const int q = num / den;
  const int r = num % den;
  if (2 * r > den) return q + 1;
  if (2 * r < den) return q;
  return (q % 2 == 0) ? q : q + 1;
}

}  // namespace

const char* modeName(Mode m) {
  switch (m) {
    case Mode::LSB:  return "LSB";
    case Mode::USB:  return "USB";
    case Mode::AM:   return "AM";
    case Mode::CW:   return "CW";
    case Mode::RTTY: return "RTTY";
    case Mode::FM:   return "FM";
    case Mode::WFM:  return "WFM";
  }
  return "?";
}

std::optional<Mode> modeFromCode(uint8_t code) {
  if (code > static_cast<uint8_t>(Mode::WFM)) return std::nullopt;
  return static_cast<Mode>(code);
}

std::optional<Mode> modeFromName(const QString& name) {
  const QString n = name.trimmed().toUpper();
  static const QMap<QString, Mode> kMap = {
      {"LSB", Mode::LSB}, {"USB", Mode::USB}, {"AM", Mode::AM},
      {"CW", Mode::CW},   {"RTTY", Mode::RTTY}, {"FM", Mode::FM},
      {"WFM", Mode::WFM}};
  const auto it = kMap.constFind(n);
  return it == kMap.constEnd() ? std::nullopt : std::optional<Mode>(it.value());
}

float smeterToDbm(int raw) {
  // 锚点：raw 0 -> S0(-127dBm 附近)，120 -> S9(-73dBm)，241 -> S9+60dB(-13dBm)
  constexpr float kS9Dbm = -73.0f;
  constexpr float kS9Raw = 120.0f;
  if (raw <= kS9Raw) {
    // 9 个 S 单位线性铺满 0..120：6 dB / S-unit
    return kS9Dbm - (kS9Raw - static_cast<float>(raw)) * (54.0f / kS9Raw);
  }
  // 120..241 线性对应 +0..+60 dB
  constexpr float kTopRaw = 241.0f;
  float above = static_cast<float>(raw) - kS9Raw;
  if (above > kTopRaw - kS9Raw) above = kTopRaw - kS9Raw;
  return kS9Dbm + above * (60.0f / (kTopRaw - kS9Raw));
}

std::vector<uint8_t> buildFrame(uint8_t to, uint8_t from,
                                std::span<const uint8_t> body,
                                int preamble_count) {
  std::vector<uint8_t> out;
  out.reserve(static_cast<size_t>(preamble_count) + body.size() + 4);
  for (int i = 0; i < preamble_count; ++i) out.push_back(kPreamble);
  out.push_back(to);
  out.push_back(from);
  out.insert(out.end(), body.begin(), body.end());
  out.push_back(kPostamble);
  return out;
}

std::optional<CivFrame> parseFrame(std::span<const uint8_t> data) {
  if (data.size() < 6) return std::nullopt;          // FE FE to from cmd FD 最短 6 字节
  if (data.front() != kPreamble) return std::nullopt;
  if (data.back() != kPostamble) return std::nullopt;
  size_t i = 0;
  while (i + 1 < data.size() && data[i] == kPreamble) ++i;  // 跳过全部前导（含与 to 冲突的极端情形由 +1 保护）
  // 此时 data[i] 是第一个非 FE 字节；剩余须至少 to/from/cmd 三字节（末尾 FD 已含在 size 内）
  if (data.size() - i < 4) return std::nullopt;      // to from cmd + FD
  CivFrame f;
  f.to = data[i];
  f.from = data[i + 1];
  f.cmd = data[i + 2];
  f.payload.assign(data.begin() + static_cast<ptrdiff_t>(i) + 3,
                   data.end() - 1);
  return f;
}

std::optional<std::array<uint8_t, 5>> freqToBcd5(uint64_t hz) {
  constexpr uint64_t kMax = 9'999'999'999ULL;  // 10 位 BCD
  if (hz > kMax) return std::nullopt;
  std::array<uint8_t, 5> msb{};                // 先按 MSB-first 填充
  for (int byte = 4; byte >= 0; --byte) {
    msb[static_cast<size_t>(byte)] =
        static_cast<uint8_t>(((hz / 10) % 10) << 4 | (hz % 10));
    hz /= 100;
  }
  std::array<uint8_t, 5> lsb{};                // 反转为线上 LSB-first
  for (size_t k = 0; k < 5; ++k) lsb[k] = msb[4 - k];
  return lsb;
}

std::optional<uint64_t> bcdToFreq(std::span<const uint8_t> bcd) {
  if (bcd.empty() || bcd.size() > 5) return std::nullopt;
  uint64_t hz = 0;
  for (size_t k = bcd.size(); k-- > 0;) {      // 从 MSB（数组尾）开始
    uint8_t b = bcd[k];
    uint8_t hi = static_cast<uint8_t>(b >> 4), lo = static_cast<uint8_t>(b & 0x0F);
    if (hi > 9 || lo > 9) return std::nullopt;
    hz = hz * 100 + static_cast<uint64_t>(hi) * 10 + lo;
  }
  return hz;
}

std::vector<uint8_t> readFreqFrame(uint8_t to, uint8_t from) {
  const uint8_t cmd[] = {kCmdReadFreq};
  return buildFrame(to, from, cmd);
}

std::vector<uint8_t> setFreqFrame(uint64_t hz, uint8_t to, uint8_t from) {
  std::vector<uint8_t> body{kCmdSetFreq};
  auto bcd = freqToBcd5(hz);
  // 上层（SafetyGate/白名单）负责拒绝越界频率；此处仅编码，超范围以全 0 兜底
  for (uint8_t b : bcd.value_or(std::array<uint8_t, 5>{})) body.push_back(b);
  return buildFrame(to, from, body);
}

std::vector<uint8_t> readModeFrame(uint8_t to, uint8_t from) {
  const uint8_t cmd[] = {kCmdReadMode};
  return buildFrame(to, from, cmd);
}

std::vector<uint8_t> setModeFrame(Mode mode, int filter, uint8_t to, uint8_t from) {
  const uint8_t body[] = {kCmdSetMode, static_cast<uint8_t>(mode),
                          static_cast<uint8_t>(filter)};
  return buildFrame(to, from, body);
}

std::vector<uint8_t> pttFrame(bool tx, uint8_t to, uint8_t from) {
  const uint8_t body[] = {kCmdPtt, 0x00, static_cast<uint8_t>(tx ? 0x01 : 0x00)};
  return buildFrame(to, from, body);
}

std::vector<uint8_t> readPttFrame(uint8_t to, uint8_t from) {
  const uint8_t body[] = {kCmdReadPtt, 0x00};
  return buildFrame(to, from, body);
}

std::vector<uint8_t> readSmeterFrame(uint8_t to, uint8_t from) {
  const uint8_t body[] = {kCmdReadSmeter, 0x03};
  return buildFrame(to, from, body);
}

std::vector<uint8_t> readMeterFrame(uint8_t to, uint8_t from) {
  const uint8_t body[] = {kCmdReadSmeter, 0x11};
  return buildFrame(to, from, body);
}

std::vector<uint8_t> attFrame(bool on_20db, uint8_t to, uint8_t from) {
  const uint8_t body[] = {kCmdAtt, static_cast<uint8_t>(on_20db ? 0x20 : 0x00)};
  return buildFrame(to, from, body);
}

std::vector<uint8_t> readAttFrame(uint8_t to, uint8_t from) {
  const uint8_t body[] = {kCmdAtt};
  return buildFrame(to, from, body);
}

std::vector<uint8_t> sw16SetFrame(uint8_t sub, uint8_t value, uint8_t to, uint8_t from) {
  const uint8_t body[] = {kCmdSw16, sub, value};
  return buildFrame(to, from, body);
}

std::vector<uint8_t> sw16ReadFrame(uint8_t sub, uint8_t to, uint8_t from) {
  const uint8_t body[] = {kCmdSw16, static_cast<uint8_t>(sub)};
  return buildFrame(to, from, body);
}

std::vector<uint8_t> duplexFrame(uint8_t val, uint8_t to, uint8_t from) {
  const uint8_t body[] = {kCmdDuplex, val};
  return buildFrame(to, from, body);
}

std::vector<uint8_t> readDuplexFrame(uint8_t to, uint8_t from) {
  const uint8_t body[] = {kCmdDuplex};
  return buildFrame(to, from, body);
}

std::vector<uint8_t> splitFrame(bool on, uint8_t to, uint8_t from) {
  const uint8_t body[] = {kCmdSplit, static_cast<uint8_t>(on ? 0x01 : 0x00)};
  return buildFrame(to, from, body);
}

std::vector<uint8_t> ritFrame(bool on, bool dtx, uint8_t to, uint8_t from) {
  const uint8_t body[] = {kCmdRit, static_cast<uint8_t>(dtx ? 0x02 : 0x01),
                          static_cast<uint8_t>(on ? 0x01 : 0x00)};
  return buildFrame(to, from, body);
}

std::vector<uint8_t> readRitFrame(bool dtx, uint8_t to, uint8_t from) {
  const uint8_t body[] = {kCmdRit, static_cast<uint8_t>(dtx ? 0x02 : 0x01)};
  return buildFrame(to, from, body);
}

std::vector<uint8_t> readOffsetFrame(uint8_t to, uint8_t from) {
  const uint8_t body[] = {0x0C};
  return buildFrame(to, from, body);
}

std::vector<uint8_t> offsetFrame(uint32_t offset_hz, uint8_t to, uint8_t from) {
  // 3B BCD（100Hz 分辨率，真机 2026-08-22 验证格式：600kHz → 00 60 00）
  offset_hz /= 100;
  const uint8_t b0 = static_cast<uint8_t>(((offset_hz / 10) % 10) << 4 |
                                          (offset_hz % 10));
  const uint8_t b1 = static_cast<uint8_t>(((offset_hz / 1000) % 10) << 4 |
                                          ((offset_hz / 100) % 10));
  const uint8_t b2 = static_cast<uint8_t>((offset_hz / 10000) % 10);
  const uint8_t body[] = {0x0D, b0, b1, b2};
  return buildFrame(to, from, body);
}

std::optional<uint64_t> parseFreqResponse(const CivFrame& f) {
  if (f.cmd != kCmdReadFreq || f.payload.size() != 5) return std::nullopt;
  return bcdToFreq(f.payload);
}

std::optional<ModeInfo> parseModeResponse(const CivFrame& f) {
  if (f.cmd != kCmdReadMode || f.payload.size() != 2) return std::nullopt;
  auto mode = modeFromCode(f.payload[0]);
  if (!mode) return std::nullopt;
  return ModeInfo{*mode, f.payload[1]};
}

std::optional<int> parseSmeterResponse(const CivFrame& f) {
  if (f.cmd != kCmdReadSmeter || f.payload.size() != 2 || f.payload[0] != 0x03)
    return std::nullopt;
  return f.payload[1];
}

std::optional<MeterInfo> parseMeterResponse(const CivFrame& f) {
  // IC-705 CI-V 手册（ci-v-spec.md:68）：0x1A 0x11 → [0x11, po(2B), swr(2B), alc(1B)]
  if (f.cmd != kCmdReadSmeter || f.payload.size() != 6 || f.payload[0] != 0x11)
    return std::nullopt;
  MeterInfo m;
  m.po_raw = (f.payload[1] << 8) | f.payload[2];
  m.swr_raw = (f.payload[3] << 8) | f.payload[4];
  m.alc = f.payload[5];
  return m;
}

std::optional<bool> parsePttResponse(const CivFrame& f) {
  if (f.cmd != kCmdReadPtt || f.payload.size() != 2 || f.payload[0] != 0x00)
    return std::nullopt;
  return f.payload[1] != 0x00;
}

std::optional<int> parseAttResponse(const CivFrame& f) {
  if (f.cmd != kCmdAtt || f.payload.size() != 1) return std::nullopt;
  return f.payload[0];
}

std::optional<int> parseSw16Response(const CivFrame& f, uint8_t sub) {
  if (f.cmd != kCmdSw16 || f.payload.size() != 2 || f.payload[0] != sub)
    return std::nullopt;
  return f.payload[1];
}

std::optional<int> parseDuplexResponse(const CivFrame& f) {
  if (f.cmd != kCmdDuplex || f.payload.size() != 1) return std::nullopt;
  return f.payload[0];
}

std::optional<bool> parseRitResponse(const CivFrame& f, bool dtx) {
  const uint8_t sub = dtx ? 0x02 : 0x01;
  if (f.cmd != kCmdRit || f.payload.size() != 2 || f.payload[0] != sub)
    return std::nullopt;
  return f.payload[1] != 0x00;
}

std::optional<uint32_t> parseOffsetResponse(const CivFrame& f) {
  if (f.cmd != 0x0C || f.payload.size() != 3) return std::nullopt;
  const auto nib = [](uint8_t b, int which) {
    return which ? (b >> 4) : (b & 0x0F);
  };
  // b0=[1kHz|100Hz] b1=[100kHz|10kHz] b2=[0固定|1MHz]
  return nib(f.payload[0], 1) * 1000ULL + nib(f.payload[0], 0) * 100ULL +
         nib(f.payload[1], 1) * 100000ULL + nib(f.payload[1], 0) * 10000ULL +
         nib(f.payload[2], 0) * 1000000ULL;
}

// ---------------- 亚音 / 数字亚音 ----------------

const char* toneSquelchName(ToneSquelch t) {
  switch (t) {
    case ToneSquelch::Off:       return "OFF";
    case ToneSquelch::Tone:      return "TONE";
    case ToneSquelch::Tsql:      return "TSQL";
    case ToneSquelch::Dtcs:      return "DTCS";
    case ToneSquelch::DtcsT:     return "DTCS(T)";
    case ToneSquelch::ToneTDtcsR: return "TONE(T)/DTCS(R)";
    case ToneSquelch::DtcsTTsqlR: return "DTCS(T)/TSQL(R)";
    case ToneSquelch::ToneTTsqlR: return "TONE(T)/TSQL(R)";
  }
  return "?";
}

std::optional<ToneSquelch> toneSquelchFromCode(uint8_t code) {
  switch (code) {
    case 0x00: return ToneSquelch::Off;
    case 0x01: return ToneSquelch::Tone;
    case 0x02: return ToneSquelch::Tsql;
    case 0x03: return ToneSquelch::Dtcs;
    case 0x06: return ToneSquelch::DtcsT;
    case 0x07: return ToneSquelch::ToneTDtcsR;
    case 0x08: return ToneSquelch::DtcsTTsqlR;
    case 0x09: return ToneSquelch::ToneTTsqlR;
    default:   return std::nullopt;
  }
}

std::vector<uint8_t> toneSquelchFrame(uint8_t type, uint8_t to, uint8_t from) {
  return sw16SetFrame(kSub16ToneSquelch, type, to, from);
}

std::vector<uint8_t> readToneSquelchFrame(uint8_t to, uint8_t from) {
  return sw16ReadFrame(kSub16ToneSquelch, to, from);
}

std::vector<uint8_t> toneSwitchFrame(uint8_t sub, bool on, uint8_t to,
                                     uint8_t from) {
  return sw16SetFrame(sub, static_cast<uint8_t>(on ? 0x01 : 0x00), to, from);
}

std::vector<uint8_t> readToneSwitchFrame(uint8_t sub, uint8_t to, uint8_t from) {
  return sw16ReadFrame(sub, to, from);
}

std::optional<int> parseToneSquelchResponse(const CivFrame& f) {
  return parseSw16Response(f, kSub16ToneSquelch);
}

std::optional<bool> parseToneSwitchResponse(const CivFrame& f, uint8_t sub) {
  const auto v = parseSw16Response(f, sub);
  if (!v) return std::nullopt;
  return *v != 0;
}

std::vector<uint8_t> toneFreqFrame(uint8_t sub, uint16_t decihz, uint8_t to,
                                   uint8_t from) {
  if (decihz > 2999) decihz = 2999;  // 100Hz 位上限 2（指南 p.21）；门禁在上层
  const auto b0 = static_cast<uint8_t>(((decihz / 1000) << 4) |
                                       ((decihz / 100) % 10));
  const auto b1 = static_cast<uint8_t>((((decihz / 10) % 10) << 4) |
                                       (decihz % 10));
  const uint8_t body[] = {kCmdTone, sub, 0x00, b0, b1};
  return buildFrame(to, from, body);
}

std::vector<uint8_t> readToneFreqFrame(uint8_t sub, uint8_t to, uint8_t from) {
  const uint8_t body[] = {kCmdTone, sub};
  return buildFrame(to, from, body);
}

std::optional<uint16_t> parseToneFreqResponse(const CivFrame& f, uint8_t sub) {
  // payload = [sub][固定 0][100Hz|10Hz][1Hz|0.1Hz]（指南 p.21）
  if (f.cmd != kCmdTone || f.payload.size() != 4 || f.payload[0] != sub ||
      f.payload[1] != 0x00)
    return std::nullopt;
  const auto nib = [](uint8_t b, int which) {
    return which ? (b >> 4) : (b & 0x0F);
  };
  const int digits[4] = {nib(f.payload[2], 1), nib(f.payload[2], 0),
                         nib(f.payload[3], 1), nib(f.payload[3], 0)};
  for (int d : digits)
    if (d > 9) return std::nullopt;
  return static_cast<uint16_t>(digits[0] * 1000 + digits[1] * 100 +
                               digits[2] * 10 + digits[3]);
}

std::vector<uint8_t> dtcsCodeFrame(const DtcsInfo& info, uint8_t to,
                                   uint8_t from) {
  int code = info.code;
  if (code < 0) code = 0;
  if (code > 777) code = 777;  // 三位八进制数字上限
  const auto pol = static_cast<uint8_t>(((info.tx_polarity & 0x01) << 4) |
                                        (info.rx_polarity & 0x01));
  const uint8_t body[] = {kCmdTone, kSubToneDtcs, pol, bcdByte(code / 100),
                          bcdByte(code % 100)};
  return buildFrame(to, from, body);
}

std::vector<uint8_t> readDtcsCodeFrame(uint8_t to, uint8_t from) {
  const uint8_t body[] = {kCmdTone, kSubToneDtcs};
  return buildFrame(to, from, body);
}

std::optional<DtcsInfo> parseDtcsCodeResponse(const CivFrame& f) {
  if (f.cmd != kCmdTone || f.payload.size() != 4 ||
      f.payload[0] != kSubToneDtcs)
    return std::nullopt;
  const int tx_pol = f.payload[1] >> 4;
  const int rx_pol = f.payload[1] & 0x0F;
  const int hi = f.payload[2] >> 4, hi_lo = f.payload[2] & 0x0F;
  const int lo_hi = f.payload[3] >> 4, lo = f.payload[3] & 0x0F;
  if (tx_pol > 1 || rx_pol > 1) return std::nullopt;
  // 4 个 BCD 位：[0][d1][d2][d3]，首位固定 0（码 023..754）
  if (hi != 0 || hi_lo > 7 || lo_hi > 7 || lo > 7) return std::nullopt;
  return DtcsInfo{hi_lo * 100 + lo_hi * 10 + lo, rx_pol, tx_pol};
}

std::span<const uint16_t> ctcssTonesDecihz() {
  // EIA 标准 50 音（0.1Hz 单位；67.0 ~ 254.1Hz）
  static constexpr uint16_t kTones[] = {
      670,  693,  719,  744,  770,  797,  825,  854,  885,  915,
      948,  974,  1000, 1035, 1072, 1109, 1148, 1188, 1230, 1273,
      1318, 1365, 1413, 1462, 1514, 1567, 1598, 1622, 1655, 1679,
      1713, 1738, 1773, 1799, 1835, 1862, 1899, 1928, 1966, 1995,
      2035, 2065, 2107, 2181, 2257, 2291, 2336, 2418, 2503, 2541};
  return kTones;
}

std::span<const uint16_t> dtcsCodes() {
  // 常用 DCS 码（三位八进制书写，数值 23..754；非穷举，避免冷门码不可选）
  static constexpr uint16_t kCodes[] = {
      23,  25,  26,  31,  32,  36,  43,  47,  51,  53,  54,  65,  71,
      72,  73,  74,  114, 115, 116, 122, 125, 131, 132, 134, 143, 145,
      152, 155, 156, 162, 165, 172, 174, 205, 212, 223, 225, 226, 243,
      244, 245, 246, 251, 252, 255, 261, 263, 265, 266, 271, 274, 306,
      311, 315, 325, 331, 332, 343, 345, 346, 351, 356, 364, 365, 371,
      411, 412, 413, 423, 431, 432, 445, 446, 452, 454, 455, 462, 464,
      465, 466, 503, 506, 516, 523, 526, 532, 546, 565, 606, 612, 624,
      627, 631, 632, 654, 662, 664, 703, 712, 723, 731, 732, 734, 743,
      754};
  return kCodes;
}


// ---------------- 双 VFO / SUB 带 ----------------

static uint8_t vfoSub(bool unselected) {
  return unselected ? kSubVfoUnselected : kSubVfoSelected;
}

std::vector<uint8_t> readVfoFreqFrame(bool unselected, uint8_t to, uint8_t from) {
  const uint8_t body[] = {kCmdVfoFreq, vfoSub(unselected)};
  return buildFrame(to, from, body);
}

std::vector<uint8_t> setVfoFreqFrame(bool unselected, uint64_t hz, uint8_t to,
                                     uint8_t from) {
  std::vector<uint8_t> body{kCmdVfoFreq, vfoSub(unselected)};
  for (uint8_t b : freqToBcd5(hz).value_or(std::array<uint8_t, 5>{}))
    body.push_back(b);
  return buildFrame(to, from, body);
}

std::vector<uint8_t> readVfoModeFrame(bool unselected, uint8_t to, uint8_t from) {
  const uint8_t body[] = {kCmdVfoMode, vfoSub(unselected)};
  return buildFrame(to, from, body);
}

std::vector<uint8_t> setVfoModeFrame(bool unselected, Mode mode, int data_mode,
                                     int filter, uint8_t to, uint8_t from) {
  // 指南 p.15/p.26：26 <sub> <mode> <DATA> <filter>；整组发送以保留 DATA/滤波
  const uint8_t body[] = {kCmdVfoMode, vfoSub(unselected),
                          static_cast<uint8_t>(mode),
                          static_cast<uint8_t>(data_mode & 0x01),
                          static_cast<uint8_t>(filter)};
  return buildFrame(to, from, body);
}

std::vector<uint8_t> selectVfoFrame(bool sub, uint8_t to, uint8_t from) {
  const uint8_t body[] = {kCmdVfoSelect, static_cast<uint8_t>(sub ? 0x01 : 0x00)};
  return buildFrame(to, from, body);
}

std::vector<uint8_t> swapVfoFrame(uint8_t to, uint8_t from) {
  const uint8_t body[] = {kCmdSwapVfo};
  return buildFrame(to, from, body);
}

std::vector<uint8_t> equalizeVfoFrame(uint8_t to, uint8_t from) {
  const uint8_t body[] = {kCmdEqualVfo};
  return buildFrame(to, from, body);
}

std::optional<VfoFreqInfo> parseVfoFreqResponse(const CivFrame& f) {
  if (f.cmd != kCmdVfoFreq || f.payload.size() < 6) return std::nullopt;
  const uint8_t sub = f.payload[0];
  if (sub != kSubVfoSelected && sub != kSubVfoUnselected) return std::nullopt;
  auto hz = bcdToFreq(std::span<const uint8_t>(f.payload).subspan(1, 5));
  if (!hz) return std::nullopt;
  return VfoFreqInfo{sub == kSubVfoUnselected, *hz};
}

std::optional<VfoModeInfo> parseVfoModeResponse(const CivFrame& f) {
  if (f.cmd != kCmdVfoMode || f.payload.size() < 2) return std::nullopt;
  const uint8_t sub = f.payload[0];
  if (sub != kSubVfoSelected && sub != kSubVfoUnselected) return std::nullopt;
  auto mode = modeFromCode(f.payload[1]);
  if (!mode) return std::nullopt;
  VfoModeInfo out;
  out.unselected = sub == kSubVfoUnselected;
  out.mode = *mode;
  out.data_mode = f.payload.size() >= 3 ? f.payload[2] : 0;
  out.filter = f.payload.size() >= 4 ? f.payload[3] : 0;
  return out;
}

// ---------------- CW Keyer ----------------

std::vector<uint8_t> cwStopFrame(uint8_t to, uint8_t from) {
  const uint8_t body[] = {kCmdCw, kCwStop};
  return buildFrame(to, from, body);
}

std::vector<std::vector<uint8_t>> cwSendFrames(std::string_view text, uint8_t to,
                                               uint8_t from) {
  // 仅送 ASCII 可打印字符（大写、空白归一为空格）；每帧 ≤30 字节（commands/cw.py）
  std::string clean;
  clean.reserve(text.size());
  for (char c : text) {
    const auto u = static_cast<unsigned char>(c);
    if (u == '\n' || u == '\r' || u == '\t') {
      clean.push_back(' ');
    } else if (u >= 32 && u <= 126) {
      clean.push_back(static_cast<char>(std::toupper(u)));
    }
  }
  std::vector<std::vector<uint8_t>> frames;
  for (size_t i = 0; i < clean.size(); i += 30) {
    const std::string chunk = clean.substr(i, 30);
    std::vector<uint8_t> body{kCmdCw};
    body.insert(body.end(), chunk.begin(), chunk.end());
    frames.push_back(buildFrame(to, from, body));
  }
  return frames;
}

std::vector<uint8_t> level14ReadFrame(uint8_t sub, uint8_t to, uint8_t from) {
  const uint8_t body[] = {kCmdLevel14, sub};
  return buildFrame(to, from, body);
}

std::vector<uint8_t> level14WriteFrame(uint8_t sub, int value, uint8_t to,
                                       uint8_t from) {
  std::vector<uint8_t> body{kCmdLevel14, sub};
  for (uint8_t b : bcdMsb(value < 0 ? 0 : (value > 255 ? 255 : value), 2))
    body.push_back(b);
  return buildFrame(to, from, body);
}

std::optional<int> parseLevel14Response(const CivFrame& f, uint8_t sub) {
  if (f.cmd != kCmdLevel14 || f.payload.size() != 3 || f.payload[0] != sub)
    return std::nullopt;
  return bcdMsbToInt(std::span<const uint8_t>(f.payload).subspan(1, 2));
}

int wpmToKeySpeedLevel(int wpm) {
  if (wpm < 6) wpm = 6;
  if (wpm > 48) wpm = 48;
  const int num = (wpm - 6) * 255;
  constexpr int kDen = 48 - 6;
  const int q = num / kDen, r = num % kDen;
  return q + (2 * r > kDen ? 1 : 0);  // nearest_half_down
}

int keySpeedLevelToWpm(int level) {
  if (level < 0) level = 0;
  if (level > 255) level = 255;
  return 6 + roundHalfEven(level * (48 - 6), 255);
}

int hzToCwPitchLevel(int hz) {
  if (hz < 300) hz = 300;
  if (hz > 900) hz = 900;
  const int num = (hz - 300) * 255;
  constexpr int kDen = 900 - 300;
  return (num + kDen - 1) / kDen;  // encode_rounding = ceil
}

int cwPitchLevelToHz(int level) {
  if (level < 0) level = 0;
  if (level > 255) level = 255;
  return 300 + 5 * roundHalfEven(level * (900 - 300), 255 * 5);
}

std::vector<uint8_t> breakInFrame(bool on, uint8_t to, uint8_t from) {
  const uint8_t body[] = {kCmdSw16, kSub16BreakIn,
                          static_cast<uint8_t>(on ? 0x01 : 0x00)};
  return buildFrame(to, from, body);
}

std::vector<uint8_t> readBreakInFrame(uint8_t to, uint8_t from) {
  const uint8_t body[] = {kCmdSw16, kSub16BreakIn};
  return buildFrame(to, from, body);
}

std::optional<bool> parseBreakInResponse(const CivFrame& f) {
  if (f.cmd != kCmdSw16 || f.payload.size() != 2 ||
      f.payload[0] != kSub16BreakIn)
    return std::nullopt;
  return f.payload[1] != 0x00;
}

// ---------------- Set Mode 菜单项 ----------------

std::vector<uint8_t> menuReadFrame(uint8_t group, uint8_t item, uint8_t to,
                                   uint8_t from) {
  const uint8_t body[] = {kCmdMenu, kSubMenu, group, item};
  return buildFrame(to, from, body);
}

std::vector<uint8_t> menuWriteFrame(uint8_t group, uint8_t item,
                                    std::span<const uint8_t> value, uint8_t to,
                                    uint8_t from) {
  std::vector<uint8_t> body{kCmdMenu, kSubMenu, group, item};
  body.insert(body.end(), value.begin(), value.end());
  return buildFrame(to, from, body);
}

std::optional<MenuValue> parseMenuResponse(const CivFrame& f) {
  if (f.cmd != kCmdMenu || f.payload.size() < 3 || f.payload[0] != kSubMenu)
    return std::nullopt;
  MenuValue out;
  out.group = f.payload[1];
  out.item = f.payload[2];
  out.value.assign(f.payload.begin() + 3, f.payload.end());
  return out;
}

// ---------------- 记忆信道 ----------------

std::vector<uint8_t> readMemoryChannelFrame(int channel, uint8_t to,
                                            uint8_t from) {
  std::vector<uint8_t> body{kCmdMenu, kSubMemoryContents};
  for (uint8_t b : bcdMsb(channel, 2)) body.push_back(b);
  return buildFrame(to, from, body);
}

std::vector<uint8_t> writeMemoryChannelFrame(const MemoryChannel& mem, uint8_t to,
                                             uint8_t from) {
  std::vector<uint8_t> body{kCmdMenu, kSubMemoryContents};
  for (uint8_t b : bcdMsb(mem.channel, 2)) body.push_back(b);
  // 26 字节内容块（rigplane commands/memory.py:build_memory_contents_set）
  body.push_back(static_cast<uint8_t>(mem.scan));
  for (uint8_t b : freqToBcd5(mem.freq_hz).value_or(std::array<uint8_t, 5>{}))
    body.push_back(b);
  body.push_back(bcdByte(static_cast<int>(mem.mode)));
  body.push_back(bcdByte(mem.filter));
  body.push_back(static_cast<uint8_t>(((mem.data_mode & 0x0F) << 4) |
                                      (mem.tone_mode & 0x0F)));
  for (uint8_t b : bcdMsb(static_cast<int>(mem.tone_freq_hz), 3)) body.push_back(b);
  for (uint8_t b : bcdMsb(static_cast<int>(mem.tsql_freq_hz), 3)) body.push_back(b);
  std::string name = mem.name.substr(0, 10);
  name.resize(10, '\0');
  body.insert(body.end(), name.begin(), name.end());
  body.push_back(0x00);  // 内容块第 26 字节（保留）
  return buildFrame(to, from, body);
}

std::vector<uint8_t> selectMemoryGroupFrame(int group, uint8_t to, uint8_t from) {
  std::vector<uint8_t> body{kCmdMemoryMode, kSubMemoryGroup};
  for (uint8_t b : bcdMsb(group, 2)) body.push_back(b);
  return buildFrame(to, from, body);
}

std::vector<uint8_t> selectMemoryChannelFrame(int channel, uint8_t to,
                                              uint8_t from) {
  std::vector<uint8_t> body{kCmdMemoryMode};
  for (uint8_t b : bcdMsb(channel, 2)) body.push_back(b);
  return buildFrame(to, from, body);
}

std::optional<MemoryChannel> parseMemoryChannelResponse(const CivFrame& f) {
  // payload = [0x00, ch(2), scan, freq(5), mode, filter, nibbles, tone(3), tsql(3), name(10), pad]
  if (f.cmd != kCmdMenu || f.payload.size() < 29 ||
      f.payload[0] != kSubMemoryContents)
    return std::nullopt;
  auto channel = bcdMsbToInt(std::span<const uint8_t>(f.payload).subspan(1, 2));
  auto mode = bcdMsbToInt(std::span<const uint8_t>(f.payload).subspan(9, 1));
  auto filter = bcdMsbToInt(std::span<const uint8_t>(f.payload).subspan(10, 1));
  auto freq = bcdToFreq(std::span<const uint8_t>(f.payload).subspan(4, 5));
  auto tone = bcdMsbToInt(std::span<const uint8_t>(f.payload).subspan(12, 3));
  auto tsql = bcdMsbToInt(std::span<const uint8_t>(f.payload).subspan(15, 3));
  if (!channel || !mode || !filter || !freq || !tone || !tsql) return std::nullopt;
  auto m = modeFromCode(static_cast<uint8_t>(*mode));
  if (!m) return std::nullopt;
  MemoryChannel out;
  out.channel = *channel;
  out.scan = f.payload[3];
  out.freq_hz = *freq;
  out.mode = *m;
  out.filter = *filter;
  out.data_mode = (f.payload[11] >> 4) & 0x0F;
  out.tone_mode = f.payload[11] & 0x0F;
  out.tone_freq_hz = static_cast<uint32_t>(*tone);
  out.tsql_freq_hz = static_cast<uint32_t>(*tsql);
  std::string name(reinterpret_cast<const char*>(f.payload.data() + 18), 10);
  while (!name.empty() && name.back() == '\0') name.pop_back();
  out.name = name;
  return out;
}

uint16_t aprsPasscode(const std::string& callsign) {
  // 公开 APRS-IS 算法：剥 SSID、大写；h=0x73e2；偶位字符直接异或、奇位左移 8
  std::string call;
  for (char c : callsign) {
    if (c == '-' || c == '\0') break;
    call.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
  }
  uint32_t h = 0x73e2;
  for (size_t i = 0; i < call.size(); ++i)
    h ^= static_cast<uint32_t>(static_cast<uint8_t>(call[i])) << ((i & 1) ? 8 : 0);
  return static_cast<uint16_t>(h & 0x7fff);
}

}  // namespace radio::core::civ
