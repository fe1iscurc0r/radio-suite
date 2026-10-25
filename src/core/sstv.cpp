// SPDX-License-Identifier: AGPL-3.0-or-later
#include "core/sstv.h"

#include <algorithm>
#include <cmath>

namespace radio::core::sstv {
namespace {

constexpr int kWidth = 320;
constexpr int kHeight = 240;

// 连续单频段（毫秒）写入采样流
void tone(std::vector<float>& out, double hz, double ms, uint32_t fs,
          double& phase) {
  const size_t n = static_cast<size_t>(std::llround(ms * fs / 1000.0));
  const double w = 2.0 * 3.14159265358979323846 * hz / fs;
  for (size_t i = 0; i < n; ++i) {
    out.push_back(static_cast<float>(0.8 * std::sin(phase)));
    phase = std::fmod(phase + w, 2.0 * 3.14159265358979323846);
  }
}

void pixelLine(std::vector<float>& out, const uint8_t* vals, uint32_t fs,
               double& phase) {
  // 整段一次性取样本数（88ms），像素间用浮点游标均分——避免每像素
  // llround 累积舍入（12.13→12 会以 -0.92ms/行 × 360 段 ≈ -331ms 漂移）
  const size_t total = static_cast<size_t>(std::llround(88.0 * fs / 1000.0));
  size_t written = 0;
  for (int x = 0; x < kWidth; ++x) {
    const size_t target =
        static_cast<size_t>(std::llround(double(total) * (x + 1) / kWidth));
    const double hz = 1500.0 + 800.0 * vals[x] / 255.0;
    const double w = 2.0 * 3.14159265358979323846 * hz / fs;
    while (written < target) {
      out.push_back(static_cast<float>(0.8 * std::sin(phase)));
      phase = std::fmod(phase + w, 2.0 * 3.14159265358979323846);
      ++written;
    }
  }
}

void visHeader(std::vector<float>& out, uint32_t fs, double& phase) {
  tone(out, 1200.0, 300.0, fs, phase);  // 起始
  tone(out, 1100.0, 30.0, fs, phase);   // break
  for (int bit = 0; bit < 7; ++bit)     // LSB first
    tone(out, (kRobot36Vis >> bit) & 1 ? 1300.0 : 1100.0, 30.0, fs, phase);
  tone(out, 1200.0, 300.0, fs, phase);  // 停止
}

}  // namespace

uint8_t toLuma(uint8_t r, uint8_t g, uint8_t b) {
  return static_cast<uint8_t>(0.299 * r + 0.587 * g + 0.114 * b + 0.5);
}
uint8_t toChromaR(uint8_t r, uint8_t g, uint8_t b) {
  return static_cast<uint8_t>(std::clamp(0.5 * r - 0.419 * g - 0.081 * b + 128.0,
                                         0.0, 255.0));
}
uint8_t toChromaB(uint8_t r, uint8_t g, uint8_t b) {
  return static_cast<uint8_t>(std::clamp(-0.169 * r - 0.331 * g + 0.5 * b + 128.0,
                                         0.0, 255.0));
}

std::vector<int16_t> encodeRobot36(const std::vector<Rgb>& img, uint32_t fs) {
  std::vector<float> pcm;
  pcm.reserve(static_cast<size_t>(37.0 * fs));
  double phase = 0.0;

  visHeader(pcm, fs, phase);

  uint8_t yrow[kWidth], crow[kWidth];
  bool chroma_is_r = true;  // 奇行色差交替：R-Y、B-Y、R-Y…
  for (int line = 0; line < kHeight; ++line) {
    const Rgb* row = &img[static_cast<size_t>(line) * kWidth];
    for (int x = 0; x < kWidth; ++x)
      yrow[x] = toLuma(row[x].r, row[x].g, row[x].b);
    tone(pcm, 1200.0, 9.0, fs, phase);   // sync
    tone(pcm, 1500.0, 3.0, fs, phase);   // porch
    pixelLine(pcm, yrow, fs, phase);
    if (line % 2 == 1) {                 // 奇行：附加色差段
      for (int x = 0; x < kWidth; ++x)
        crow[x] = chroma_is_r ? toChromaR(row[x].r, row[x].g, row[x].b)
                              : toChromaB(row[x].r, row[x].g, row[x].b);
      tone(pcm, 1200.0, 9.0, fs, phase);
      tone(pcm, 1500.0, 3.0, fs, phase);
      pixelLine(pcm, crow, fs, phase);
      chroma_is_r = !chroma_is_r;
    }
  }

  std::vector<int16_t> out(pcm.size());
  for (size_t i = 0; i < pcm.size(); ++i)
    out[i] = static_cast<int16_t>(std::lround(pcm[i] * 32767.0));
  return out;
}

}  // namespace radio::core::sstv
