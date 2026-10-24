// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/hex_util.h — fixtures 十六进制读写辅助（仅测试目标）
#pragma once

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace radio::test {

inline std::string toHex(std::span<const uint8_t> bytes) {
  std::string out;
  out.reserve(bytes.size() * 2);
  static const char* kDigits = "0123456789abcdef";
  for (uint8_t b : bytes) {
    out.push_back(kDigits[b >> 4]);
    out.push_back(kDigits[b & 0x0F]);
  }
  return out;
}

inline std::vector<uint8_t> fromHex(const std::string& line) {
  std::vector<uint8_t> out;
  int hi = -1;
  for (char c : line) {
    if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
    int v;
    if (c >= '0' && c <= '9') v = c - '0';
    else if (c >= 'a' && c <= 'f') v = c - 'a' + 10;
    else if (c >= 'A' && c <= 'F') v = c - 'A' + 10;
    else throw std::runtime_error(std::string("bad hex char: ") + c);
    if (hi < 0) hi = v;
    else {
      out.push_back(static_cast<uint8_t>((hi << 4) | v));
      hi = -1;
    }
  }
  if (hi >= 0) throw std::runtime_error("odd hex length");
  return out;
}

// 每行一帧；空行与 # 注释跳过
inline std::vector<std::vector<uint8_t>> loadHexFrames(const std::string& path) {
  std::ifstream in(path);
  if (!in) throw std::runtime_error("cannot open fixture: " + path);
  std::vector<std::vector<uint8_t>> frames;
  std::string line;
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.empty() || line[0] == '#') continue;
    frames.push_back(fromHex(line));
  }
  return frames;
}

inline std::vector<uint8_t> loadSingleFrame(const std::string& path) {
  auto frames = loadHexFrames(path);
  if (frames.size() != 1)
    throw std::runtime_error("expected exactly 1 frame in " + path);
  return frames.front();
}

}  // namespace radio::test
