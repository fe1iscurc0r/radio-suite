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
  return encodeSstv(Mode::Robot36, img, fs);
}

#if 0  // 旧实现由 encodeSstv(Robot36) 替代
std::vector<int16_t> encodeRobot36_old(const std::vector<Rgb>& img, uint32_t fs) {
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
#endif  // 旧实现

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
  return decodeSstv(pcm, fs);
}

// ---------------- 多制式（师傅指示：电脑编解码 = 制式自由） ----------------
namespace {

struct ModeSpec {
  uint8_t vis;
  int width, height;
  double line_ms;    // 名义行时长（时长恒等式用）
};

constexpr ModeSpec kSpec[] = {
    {0x08, 320, 240, 200.0},  // Robot36：偶 100ms / 奇 200ms
    {0x0C, 320, 240, 300.0},  // Robot72：每行 300ms
    {44,  320, 256, 447.452},  // Martin M1
    {40,  320, 256, 226.834},  // Martin M2
    {60,  320, 256, 428.22},   // Scottie S1
    {56,  320, 256, 216.06},   // Scottie S2
    {96,  640, 496, 0},        // PD180（时长恒等式另算：2 行一组）
    {95,  640, 496, 0},        // PD120
};

void visHeaderVis(std::vector<float>& out, uint8_t vis, uint32_t fs,
                  double& phase) {
  // VoX 校准头（对齐上游 SSTVEncoder2 Mode.writeCalibrationHeader，
  // 亦为行业互操作标准）：
  //   1900Hz 300ms + 1200Hz 10ms + 1900Hz 300ms
  //   + 1200Hz 30ms（起始位）+ 7 位数据（LSB first，1300=1/1100=0，
  //   30ms/位）+ 奇偶校验位（偶校验）+ 1200Hz 30ms（停止位）
  tone(out, 1900.0, 300.0, fs, phase);
  tone(out, 1200.0, 10.0, fs, phase);
  tone(out, 1900.0, 300.0, fs, phase);
  tone(out, 1200.0, 30.0, fs, phase);
  int parity = 0;
  for (int pos = 0; pos < 7; ++pos) {
    const int bit = (vis >> pos) & 1;
    parity ^= bit;
    tone(out, bit ? 1300.0 : 1100.0, 30.0, fs, phase);
  }
  tone(out, parity ? 1300.0 : 1100.0, 30.0, fs, phase);
  tone(out, 1200.0, 30.0, fs, phase);
}

}  // namespace

const char* modeName(Mode m) {
  switch (m) {
    case Mode::Robot36: return "Robot 36";
    case Mode::Robot72: return "Robot 72";
    case Mode::Martin1: return "Martin M1";
    case Mode::Martin2: return "Martin M2";
    case Mode::Scottie1: return "Scottie 1";
    case Mode::Scottie2: return "Scottie 2";
    case Mode::PD180: return "PD 180";
    case Mode::PD120: return "PD 120";
  }
  return "?";
}

std::optional<Mode> modeFromVis(uint8_t vis) {
  switch (vis) {
    case 0x08: return Mode::Robot36;
    case 0x0C: return Mode::Robot72;
    case 44: return Mode::Martin1;
    case 40: return Mode::Martin2;
    case 60: return Mode::Scottie1;
    case 56: return Mode::Scottie2;
    case 96: return Mode::PD180;
    case 95: return Mode::PD120;
  }
  return std::nullopt;
}

int imageWidth(Mode m) { return kSpec[static_cast<int>(m)].width; }
int imageHeight(Mode m) { return kSpec[static_cast<int>(m)].height; }

std::vector<int16_t> encodeSstv(Mode m, const std::vector<Rgb>& img,
                                uint32_t fs) {
  const ModeSpec& sp = kSpec[static_cast<int>(m)];
  std::vector<float> pcm;
  double phase = 0.0;
  visHeaderVis(pcm, sp.vis, fs, phase);

  const auto emit_channel = [&](const uint8_t* vals, double scan_ms) {
    const size_t total =
        static_cast<size_t>(std::llround(scan_ms * fs / 1000.0));
    size_t written = 0;
    for (int x = 0; x < sp.width; ++x) {
      const size_t target =
          static_cast<size_t>(std::llround(double(total) * (x + 1) / sp.width));
      const double hz = 1500.0 + 800.0 * vals[x] / 255.0;
      const double w = 2.0 * 3.14159265358979323846 * hz / fs;
      while (written < target) {
        pcm.push_back(static_cast<float>(0.8 * std::sin(phase)));
        phase = std::fmod(phase + w, 2.0 * 3.14159265358979323846);
        ++written;
      }
    }
  };

  uint8_t row[640];
  if (m == Mode::Martin1 || m == Mode::Martin2) {
    // Martin（对齐上游）：sync 1200Hz 4.862ms + G + sep 0.572ms + B + sep + R
    const double scan = m == Mode::Martin1 ? 146.432 : 73.216;
    for (int line = 0; line < sp.height; ++line) {
      const Rgb* src = &img[static_cast<size_t>(line) * sp.width];
      tone(pcm, 1200.0, 4.862, fs, phase);
      uint8_t gr[640], bl[640], rd[640];
      for (int x = 0; x < sp.width; ++x) {
        gr[x] = src[x].g; bl[x] = src[x].b; rd[x] = src[x].r;
      }
      emit_channel(gr, scan);
      tone(pcm, 1200.0, 0.572, fs, phase);
      emit_channel(bl, scan);
      tone(pcm, 1200.0, 0.572, fs, phase);
      emit_channel(rd, scan);
    }
  } else if (m == Mode::Scottie1 || m == Mode::Scottie2) {
    // Scottie（对齐上游 Scottie.writeEncodedLine）：
    //   sep + G + sep + B + sync 9ms + syncPorch 1.5ms + R（行首 sync 后置）
    const double scan = m == Mode::Scottie1 ? 138.240 : 69.120;
    for (int line = 0; line < sp.height; ++line) {
      const Rgb* src = &img[static_cast<size_t>(line) * sp.width];
      uint8_t gr[640], bl[640], rd[640];
      for (int x = 0; x < sp.width; ++x) {
        gr[x] = src[x].g; bl[x] = src[x].b; rd[x] = src[x].r;
      }
      tone(pcm, 1500.0, 1.5, fs, phase);   // separator
      emit_channel(gr, scan);
      tone(pcm, 1500.0, 1.5, fs, phase);   // separator
      emit_channel(bl, scan);
      tone(pcm, 1200.0, 9.0, fs, phase);   // sync pulse
      tone(pcm, 1500.0, 1.5, fs, phase);   // sync porch
      emit_channel(rd, scan);
    }
  } else if (m == Mode::PD180 || m == Mode::PD120) {
    // PD（对齐上游 PD.writeEncodedLine）：每 2 行一组
    //   sync 20ms + porch 2.08ms + Y(line) + V(line) + U(line) + Y(line+1)
    // YUV440P：V/U 每两行一组。色彩标度：BT.601 全摆幅（V/U 128 居中）
    const double scan = m == Mode::PD180 ? 183.04 : 121.6;
    auto yuvAt = [&](int x, int line, int which) -> uint8_t {
      // 偶数行组共享 V/U（line/2）
      const Rgb& px = img[static_cast<size_t>(line) * sp.width + x];
      if (which == 0) return toLuma(px.r, px.g, px.b);
      const uint8_t v = toChromaR(px.r, px.g, px.b);
      const uint8_t u = toChromaB(px.r, px.g, px.b);
      return which == 1 ? v : u;
    };
    for (int line = 0; line < sp.height; line += 2) {
      tone(pcm, 1200.0, 20.0, fs, phase);
      tone(pcm, 1500.0, 2.08, fs, phase);
      for (int sub = 0; sub < 4; ++sub) {
        const int which = sub == 0 ? 0 : sub == 1 ? 1 : sub == 2 ? 2 : 0;
        const int src_line = (sub == 3) ? line + 1 : line;
        for (int x = 0; x < sp.width; ++x)
          row[x] = yuvAt(x, src_line, which);
        emit_channel(row, scan);
      }
    }
  } else if (m == Mode::Robot72) {
    // Robot72：每行 sync9+porch3 + Y + sync9+porch3 + R-Y + sync9+porch3 + B-Y
    for (int line = 0; line < sp.height; ++line) {
      const Rgb* src = &img[static_cast<size_t>(line) * sp.width];
      const auto emit_rgb_channel = [&](int which) {
        for (int x = 0; x < sp.width; ++x)
          row[x] = which == 0 ? toLuma(src[x].r, src[x].g, src[x].b)
                              : (which == 1 ? toChromaR(src[x].r, src[x].g, src[x].b)
                                            : toChromaB(src[x].r, src[x].g, src[x].b));
        emit_channel(row, 88.0);
      };
      tone(pcm, 1200.0, 9.0, fs, phase);
      tone(pcm, 1500.0, 3.0, fs, phase);
      emit_rgb_channel(0);
      tone(pcm, 1200.0, 9.0, fs, phase);
      tone(pcm, 1500.0, 3.0, fs, phase);
      emit_rgb_channel(1);
      tone(pcm, 1200.0, 9.0, fs, phase);
      tone(pcm, 1500.0, 3.0, fs, phase);
      emit_rgb_channel(2);
    }
  } else {
    // Robot36：偶行 sync9+porch3+Y；奇行 + sync9+porch3+色差（R-Y/B-Y 交替）
    bool chroma_is_r = true;
    for (int line = 0; line < sp.height; ++line) {
      const Rgb* src = &img[static_cast<size_t>(line) * sp.width];
      const auto emit_ycc = [&](int which) {
        for (int x = 0; x < sp.width; ++x)
          row[x] = which == 0 ? toLuma(src[x].r, src[x].g, src[x].b)
                              : (which == 1 ? toChromaR(src[x].r, src[x].g, src[x].b)
                                            : toChromaB(src[x].r, src[x].g, src[x].b));
        emit_channel(row, 88.0);
      };
      tone(pcm, 1200.0, 9.0, fs, phase);
      tone(pcm, 1500.0, 3.0, fs, phase);
      emit_ycc(0);
      if (line % 2 == 1) {
        tone(pcm, 1200.0, 9.0, fs, phase);
        tone(pcm, 1500.0, 3.0, fs, phase);
        emit_ycc(chroma_is_r ? 1 : 2);
        chroma_is_r = !chroma_is_r;
      }
    }
  }

  std::vector<int16_t> out(pcm.size());
  for (size_t i = 0; i < pcm.size(); ++i)
    out[i] = static_cast<int16_t>(std::lround(pcm[i] * 32767.0));
  return out;
}

std::vector<Rgb> decodeRobot36At(const std::vector<int16_t>& pcm, size_t start,
                                 uint32_t fs);
std::vector<Rgb> decodeChannels(Mode m, const std::vector<int16_t>& pcm,
                                size_t start, uint32_t fs);

std::vector<Rgb> decodeSstv(const std::vector<int16_t>& pcm, uint32_t fs) {
  // 通用 VIS 扫描（VoX 校准头，与 writeCalibrationHeader 对齐）：
  //   [1900×300][1200×10][1900×300][1200×30 起][7 数据位×30][奇偶×30][1200×30 止]
  //   扫描锚点 = 前导第二段 1900Hz×300ms；位区从其后 310ms 开始
  const size_t ms10 = static_cast<size_t>(10.0 * fs / 1000.0);
  const size_t ms30 = static_cast<size_t>(30.0 * fs / 1000.0);
  const size_t ms300 = static_cast<size_t>(300.0 * fs / 1000.0);
  if (pcm.size() < ms300 * 2 + ms10 + ms30 * 9) return {};
  for (size_t off = 0; off + ms300 * 2 + ms10 + ms30 * 9 <= pcm.size();
       off += ms30) {
    if (std::abs(sniffHz(pcm, off, ms300, fs) - 1900.0) > 45.0) continue;
    // 第二段 1900（确认校准头而非普通 1200 同步）
    const size_t after_break = off + ms300 + ms10;
    if (std::abs(sniffHz(pcm, after_break, ms300, fs) - 1900.0) > 45.0)
      continue;
    const size_t start_bit = after_break + ms300 + ms30;  // 跳过起始位
    uint8_t vis = 0;
    bool ok = true;
    for (int bit = 0; bit < 7 && ok; ++bit) {
      const double hz =
          sniffHz(pcm, start_bit + static_cast<size_t>(bit) * ms30, ms30, fs);
      if (std::abs(hz - 1300.0) <= 45.0) vis |= 1 << bit;
      else if (std::abs(hz - 1100.0) > 45.0) ok = false;
    }
    if (!ok) continue;
    // 校验位 + 停止位 1200
    if (std::abs(sniffHz(pcm, start_bit + 8 * ms30, ms30, fs) - 1200.0) > 45.0)
      continue;
    const auto mode = modeFromVis(vis);
    if (!mode) return {};
    const size_t start = start_bit + 9 * ms30;  // 数据起点
    if (*mode == Mode::Robot36)
      return decodeRobot36At(pcm, start, fs);
    return decodeChannels(*mode, pcm, start, fs);
  }
  return {};
}

// Robot36 帧体解码（起点 = VIS 后第一字节；由 decodeSstv 定位）
std::vector<Rgb> decodeRobot36At(const std::vector<int16_t>& pcm, size_t start,
                                 uint32_t fs) {
  std::vector<Rgb> img;

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

// ---------------- Martin / Robot72 通道级解码 ----------------
std::vector<Rgb> decodeChannels(Mode m, const std::vector<int16_t>& pcm,
                                size_t start, uint32_t fs) {
  const ModeSpec& sp = kSpec[static_cast<int>(m)];
  const double scan_ms = m == Mode::Martin1   ? 146.432
                         : m == Mode::Martin2 ? 73.216
                                              : 88.0;
  const size_t scan_n = static_cast<size_t>(std::llround(scan_ms * fs / 1000.0));
  const size_t sync_n = static_cast<size_t>(
      std::llround((m == Mode::Martin1 || m == Mode::Martin2 ? 4.862 : 9.0) * fs / 1000.0));
  const size_t gap_n = static_cast<size_t>(std::llround(
      (m == Mode::Martin1 || m == Mode::Martin2 ? 0.572 : 3.0) * fs / 1000.0));

  std::vector<Rgb> img(static_cast<size_t>(sp.width) * sp.height);
  std::vector<uint8_t> ch[3] = {
      std::vector<uint8_t>(sp.width), std::vector<uint8_t>(sp.width),
      std::vector<uint8_t>(sp.width)};

  size_t pos = start;
  for (int line = 0; line < sp.height; ++line) {
    for (int c = 0; c < 3; ++c) {
      pos += (c == 0) ? sync_n : gap_n;  // 行首 sync；通道间 sep/porch
      if (m == Mode::Martin1 || m == Mode::Martin2) {
        pos += (c == 0) ? 0 : gap_n;     // Martin：sep 后直接通道
      } else {
        pos += (c == 0) ? gap_n : sync_n + gap_n;  // Robot72：sync+porch
      }
      if (pos + scan_n > pcm.size()) return img;
      const auto inst = demodulate(pcm, pos, scan_n, fs);
      for (int x = 0; x < sp.width; ++x) {
        const size_t c0 = static_cast<size_t>(x) * scan_n / sp.width;
        const size_t c1 = static_cast<size_t>(x + 1) * scan_n / sp.width;
        double f = 0; size_t cnt = 0;
        for (size_t i = c0; i < c1 && i < inst.size(); ++i, ++cnt) f += inst[i];
        ch[c][static_cast<size_t>(x)] = hzToLevel(cnt ? f / cnt : 1500.0);
      }
      pos += scan_n;
    }
    // 落图：Martin 通道序 G,B,R；Robot72 为 Y,R-Y,B-Y
    for (int x = 0; x < sp.width; ++x) {
      const size_t i = static_cast<size_t>(line) * sp.width + x;
      if (m == Mode::Martin1 || m == Mode::Martin2) {
        img[i] = {ch[2][static_cast<size_t>(x)], ch[0][static_cast<size_t>(x)],
                  ch[1][static_cast<size_t>(x)]};
      } else {
        const int y = ch[0][static_cast<size_t>(x)];
        const int r = y + (ch[1][static_cast<size_t>(x)] - 128) * 1.402;
        const int b = y + (ch[2][static_cast<size_t>(x)] - 128) * 1.772;
        const int g = (y - 0.299 * r - 0.114 * b) / 0.587;
        img[i] = {static_cast<uint8_t>(std::clamp(r, 0, 255)),
                  static_cast<uint8_t>(std::clamp(g, 0, 255)),
                  static_cast<uint8_t>(std::clamp(b, 0, 255))};
      }
    }
  }
  return img;
}


}  // namespace radio::core::sstv
