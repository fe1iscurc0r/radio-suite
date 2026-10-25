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

// ---------------- 解码 ----------------
namespace {

// 单采样点主导频率（窗内 Goertzel 打分，步进 25Hz；解码逐段用，窗 = 段长）
double sniffHz(const std::vector<int16_t>& s, size_t begin, size_t len,
               uint32_t fs) {
  double best_hz = 0, best_p = -1;
  for (double hz = 1050.0; hz <= 2350.0; hz += 25.0) {
    const double k = std::round(len * hz / fs);
    const double w = 2.0 * 3.14159265358979323846 * k / len;
    const double coeff = 2.0 * std::cos(w);
    double q0 = 0, q1 = 0, q2 = 0;
    for (size_t i = 0; i < len; ++i) {
      q0 = coeff * q1 - q2 + s[begin + i];
      q2 = q1;
      q1 = q0;
    }
    const double p = q1 * q1 + q2 * q2 - coeff * q1 * q2;
    if (p > best_p) {
      best_p = p;
      best_hz = hz;
    }
  }
  return best_hz;
}

inline uint8_t hzToLevel(double hz) {
  return static_cast<uint8_t>(std::clamp((hz - 1500.0) / 800.0 * 255.0, 0.0, 255.0));
}

// 延迟鉴频器（delay discriminator）：v = LPF(s·s_d)/LPF(s²) ≈ cos(2πf·d/fs)。
// d 取中频半周期（≈11 样本）时 1500-2300Hz 落在 acos 单调区间，样本级出频率；
// EMA 平滑 ~1ms。逐像素 FFT 在 275µs/px 下不可分辨（<1 周期），此为正路。
std::vector<double> demodulate(const std::vector<int16_t>& s, size_t begin,
                               size_t n, uint32_t fs) {
  // d 按最高频率 2300Hz + 裕量取（无混叠范围 f ≤ fs/2d）：
  // d=9 → 上限 2450Hz，1500-2300 全落在 acos 单调区间 [1.92, 2.95] rad
  const size_t d = static_cast<size_t>(std::llround(fs / 4900.0));  // ≈9
  std::vector<double> freq(n, 1900.0);
  double lp_num = 0, lp_den = 0, lp_f = 1900.0;
  const double a = 0.02;  // ~1.1ms 低通
  const double af = 0.05; // 频率平滑
  for (size_t i = d; i < n; ++i) {
    const double x0 = s[begin + i];
    const double xd = s[begin + i - d];
    lp_num += a * (x0 * xd - lp_num);
    lp_den += a * (x0 * x0 - lp_den);
    double corr = lp_den > 1 ? lp_num / lp_den : 1.0;
    corr = std::clamp(corr, -1.0, 1.0);
    const double f = std::acos(corr) * fs / (2.0 * 3.14159265358979323846 * d);
    lp_f += af * (f - lp_f);
    freq[i] = lp_f;
  }
  return freq;
}

// 在 PCM 中扫描 VIS=8 引导头（1200 起 + 7 位 + 1200 止），返回数据起始偏移
size_t findVis(const std::vector<int16_t>& s, uint32_t fs) {
  const size_t ms30 = static_cast<size_t>(30.0 * fs / 1000.0);
  const size_t ms300 = static_cast<size_t>(300.0 * fs / 1000.0);
  if (s.size() < ms300 * 2 + ms30 * 8) return 0;
  for (size_t off = 0; off + ms300 * 2 + ms30 * 8 <= s.size(); off += ms30) {
    if (std::abs(sniffHz(s, off, ms300, fs) - 1200.0) > 45.0) continue;
    const size_t bit0 = off + ms300 + ms30;  // 跳过 break
    bool ok = true;
    for (int bit = 0; bit < 7 && ok; ++bit) {
      const double hz = sniffHz(s, bit0 + static_cast<size_t>(bit) * ms30,
                                ms30, fs);
      const int want = (kRobot36Vis >> bit) & 1;
      if (std::abs(hz - (want ? 1300.0 : 1100.0)) > 45.0) ok = false;
    }
    if (!ok) continue;
    if (std::abs(sniffHz(s, bit0 + 7 * ms30, ms300, fs) - 1200.0) > 45.0)
      continue;
    return bit0 + 7 * ms30 + ms300;  // 图像数据起点
  }
  return 0;
}

}  // namespace

std::vector<Rgb> decodeRobot36(const std::vector<int16_t>& pcm, uint32_t fs) {
  std::vector<Rgb> img;
  const size_t start = findVis(pcm, fs);
  if (start == 0 || start >= pcm.size()) return img;

  const size_t sync_n = static_cast<size_t>(std::llround(9.0 * fs / 1000.0));
  const size_t porch_n = static_cast<size_t>(std::llround(3.0 * fs / 1000.0));
  const size_t scan_n = static_cast<size_t>(std::llround(88.0 * fs / 1000.0));
  img.resize(static_cast<size_t>(kWidth) * kHeight);

  // Y / 交替色差行缓存（解码侧 R-Y、B-Y 交替恢复彩色）
  std::vector<uint8_t> yrow(kWidth), cr(kWidth), cb(kWidth);
  bool chroma_is_r = true, have_cr = false, have_cb = false;

  size_t pos = start;
  for (int line = 0; line < kHeight; ++line) {
    // sync + porch
    pos += sync_n + porch_n;
    if (pos + scan_n > pcm.size()) break;
    const auto inst = demodulate(pcm, pos, scan_n, fs);  // 鉴频出逐样本频率
    for (int x = 0; x < kWidth; ++x) {
      const size_t c0 = static_cast<size_t>(x) * scan_n / kWidth;
      const size_t c1 = static_cast<size_t>(x + 1) * scan_n / kWidth;
      double f = 0;
      size_t cnt = 0;
      for (size_t i = c0; i < c1 && i < inst.size(); ++i, ++cnt) f += inst[i];
      yrow[static_cast<size_t>(x)] =
          hzToLevel(cnt ? f / cnt : 1500.0);
    }
    pos += scan_n;

    if (line % 2 == 1) {  // 奇行色差段
      pos += sync_n + porch_n;
      if (pos + scan_n > pcm.size()) break;
      const auto cinst = demodulate(pcm, pos, scan_n, fs);
      for (int x = 0; x < kWidth; ++x) {
        const size_t c0 = static_cast<size_t>(x) * scan_n / kWidth;
        const size_t c1 = static_cast<size_t>(x + 1) * scan_n / kWidth;
        double f = 0;
        size_t cnt = 0;
        for (size_t i = c0; i < c1 && i < cinst.size(); ++i, ++cnt) f += cinst[i];
        (chroma_is_r ? cr : cb)[static_cast<size_t>(x)] =
            hzToLevel(cnt ? f / cnt : 1500.0);
      }
      pos += scan_n;
      if (chroma_is_r) have_cr = true; else have_cb = true;
      chroma_is_r = !chroma_is_r;
    }

    // 亮度行落图（偶行只有 Y；奇行 Y 已在上一奇/偶对齐——逐行都写 Y）
    for (int x = 0; x < kWidth; ++x) {
      const size_t i = static_cast<size_t>(line) * kWidth + x;
      const int y = yrow[static_cast<size_t>(x)];
      if (have_cr && have_cb) {
        const int r = y + (cr[static_cast<size_t>(x)] - 128) * 1.402;
        const int b = y + (cb[static_cast<size_t>(x)] - 128) * 1.772;
        const int g = (y - 0.299 * r - 0.114 * b) / 0.587;
        img[i] = {static_cast<uint8_t>(std::clamp(r, 0, 255)),
                  static_cast<uint8_t>(std::clamp(g, 0, 255)),
                  static_cast<uint8_t>(std::clamp(b, 0, 255))};
      } else {
        img[i] = {static_cast<uint8_t>(y), static_cast<uint8_t>(y),
                  static_cast<uint8_t>(y)};
      }
    }
  }
  return img;
}

}  // namespace radio::core::sstv
