// SPDX-License-Identifier: AGPL-3.0-or-later
#include "core/civ.h"

#include <QMap>

#include <cctype>
#include <cmath>

namespace radio::core::civ {

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
  const uint8_t body[] = {kCmdPtt, 0x00, tx ? 0x01 : 0x00};
  return buildFrame(to, from, body);
}

std::vector<uint8_t> readPttFrame(uint8_t to, uint8_t from) {
  const uint8_t body[] = {kCmdReadPtt, 0x0C};
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
  if (f.cmd != kCmdReadPtt || f.payload.size() != 2 || f.payload[0] != 0x0C)
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
