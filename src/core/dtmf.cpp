// SPDX-License-Identifier: AGPL-3.0-or-later
#include "core/dtmf.h"

#include <algorithm>
#include <cmath>

namespace radio::core::dtmf {
namespace {

// ITU Q.23 频率表
constexpr double kRows[4] = {697.0, 770.0, 852.0, 941.0};
constexpr double kCols[4] = {1209.0, 1336.0, 1477.0, 1633.0};
constexpr char kKeys[4][4] = {
    {'1', '2', '3', 'A'}, {'4', '5', '6', 'B'},
    {'7', '8', '9', 'C'}, {'*', '0', '#', 'D'}};

double goertzelPower(const std::vector<int16_t>& s, size_t begin, size_t len,
                     double hz, uint32_t fs) {
  const double k = std::round(len * hz / fs);
  const double w = 2.0 * 3.14159265358979323846 * k / len;
  const double coeff = 2.0 * std::cos(w);
  double q0 = 0, q1 = 0, q2 = 0;
  for (size_t i = 0; i < len; ++i) {
    q0 = coeff * q1 - q2 + s[begin + i];
    q2 = q1;
    q1 = q0;
  }
  return q1 * q1 + q2 * q2 - coeff * q1 * q2;
}

}  // namespace

bool isDigit(char c) {
  return (c >= '0' && c <= '9') || c == '*' || c == '#' ||
         (c >= 'A' && c <= 'D');
}

std::pair<double, double> freqsOf(char c) {
  for (int r = 0; r < 4; ++r)
    for (int q = 0; q < 4; ++q)
      if (kKeys[r][q] == c) return {kRows[r], kCols[q]};
  return {0.0, 0.0};
}

std::vector<int16_t> encode(const std::string& digits, uint32_t fs,
                            double tone_ms, double gap_ms) {
  std::vector<float> pcm;
  double phase1 = 0.0, phase2 = 0.0;
  const auto add_tone = [&](double f1, double f2, double ms) {
    const size_t n = static_cast<size_t>(std::llround(ms * fs / 1000.0));
    const double w1 = 2.0 * 3.14159265358979323846 * f1 / fs;
    const double w2 = 2.0 * 3.14159265358979323846 * f2 / fs;
    for (size_t i = 0; i < n; ++i) {
      pcm.push_back(static_cast<float>(0.4 * (std::sin(phase1) + std::sin(phase2))));
      phase1 = std::fmod(phase1 + w1, 2.0 * 3.14159265358979323846);
      phase2 = std::fmod(phase2 + w2, 2.0 * 3.14159265358979323846);
    }
  };
  for (char c : digits) {
    if (!isDigit(c)) continue;
    auto [f1, f2] = freqsOf(c);
    phase1 = phase2 = 0.0;
    add_tone(f1, f2, tone_ms);
    pcm.resize(pcm.size() + static_cast<size_t>(gap_ms * fs / 1000.0), 0.0f);
  }
  std::vector<int16_t> out(pcm.size());
  for (size_t i = 0; i < pcm.size(); ++i)
    out[i] = static_cast<int16_t>(std::lround(pcm[i] * 32767.0));
  return out;
}

std::string decode(const std::vector<int16_t>& pcm, uint32_t fs) {
  std::string out;
  const size_t win = static_cast<size_t>(40.0 * fs / 1000.0);   // 40ms 窗
  const size_t step = static_cast<size_t>(20.0 * fs / 1000.0);  // 20ms 步进
  if (pcm.size() < win) return out;

  char prev = 0;
  int hold = 0;
  const int min_hold = 3;  // ≥60ms 视为有效键
  for (size_t off = 0; off + win <= pcm.size(); off += step) {
    // 行频 / 列频各自最强
    int best_r = -1, best_c = -1;
    double pr = -1, pc = -1, energy = 0;
    for (int r = 0; r < 4; ++r) {
      const double p = goertzelPower(pcm, off, win, kRows[r], fs);
      energy += p;
      if (p > pr) { pr = p; best_r = r; }
    }
    for (int q = 0; q < 4; ++q) {
      const double p = goertzelPower(pcm, off, win, kCols[q], fs);
      energy += p;
      if (p > pc) { pc = p; best_c = q; }
    }
    const char cur = (best_r >= 0 && best_c >= 0 && energy > 1e9)
                         ? kKeys[best_r][best_c] : 0;
    if (cur == prev) {
      if (cur && ++hold == min_hold) out.push_back(cur);
    } else {
      hold = cur ? 1 : 0;
    }
    prev = cur;
  }
  return out;
}

}  // namespace radio::core::dtmf
