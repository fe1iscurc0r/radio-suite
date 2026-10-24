// SPDX-License-Identifier: AGPL-3.0-or-later
#include "core/civ.h"

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

}  // namespace radio::core::civ
