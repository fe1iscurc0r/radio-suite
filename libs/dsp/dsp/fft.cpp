// SPDX-License-Identifier: AGPL-3.0-or-later
#include "dsp/fft.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

#include "pocketfft_hdronly.h"

namespace radio::dsp {

namespace {
constexpr double kTwoPi = 6.283185307179586476925286766559;
}

SpectrumAnalyzer::SpectrumAnalyzer(size_t fft_size, double sample_rate)
    : n_(fft_size), sample_rate_(sample_rate) {
  if (n_ < 8 || n_ > (1u << 22)) throw std::invalid_argument("fft_size out of range [8, 4M]");
  if (sample_rate_ <= 0.0) throw std::invalid_argument("sample_rate must be > 0");

  window_.resize(n_);
  windowed_.assign(n_, 0.0f);
  spec_.assign(n_ / 2 + 1, {});
  power_db_.assign(n_ / 2 + 1, -200.0f);
  avg_power_.assign(n_ / 2 + 1, 0.0f);

  window_sum_ = 0.0;
  for (size_t i = 0; i < n_; ++i) {
    // 周期 Hann（分母 N）：50% 重叠时满足 COLA，且是频谱分析的常规选择；
    // 对称窗（N-1）是滤波器设计口径，会让重叠相加后幅度有波纹。
    window_[i] = static_cast<float>(
        0.5 * (1.0 - std::cos(kTwoPi * static_cast<double>(i) /
                              static_cast<double>(n_))));
    window_sum_ += window_[i];
  }

  shape_ = {n_};
  stride_in_ = {static_cast<ptrdiff_t>(sizeof(float))};
  stride_out_ = {static_cast<ptrdiff_t>(sizeof(std::complex<float>))};
}

void SpectrumAnalyzer::process(const float* samples, size_t n) {
  if (n != n_) throw std::invalid_argument("process() expects exactly fft_size samples");

  for (size_t i = 0; i < n_; ++i) windowed_[i] = samples[i] * window_[i];

  pocketfft::r2c<float>(shape_, stride_in_, stride_out_, 0, true,
                        windowed_.data(), spec_.data(), 1.0f, 1);

  // 幅度归一：A = |X| / Σw；bin0 与 Nyquist 无 ×2（单边谱共轭对称补偿）。
  // 平均在线性功率域做（1/k 递归），dB 只作输出——避免对 dB 取平均。
  const double norm = window_sum_;
  const float alpha = 1.0f / static_cast<float>(avg_frames_);
  const size_t last = spec_.size() - 1;
  for (size_t k = 0; k < spec_.size(); ++k) {
    double mag = std::abs(spec_[k]) / norm;
    if (k != 0 && k != last) mag *= 2.0;
    const float p = static_cast<float>(mag * mag);
    const float avg = avg_seeded_ ? avg_power_[k] + alpha * (p - avg_power_[k])
                                  : p;
    avg_power_[k] = avg;
    power_db_[k] = 10.0f * std::log10(avg + 1e-24f);
  }
  avg_seeded_ = true;
  ++seq_;
}

void SpectrumAnalyzer::setAveragingFrames(size_t frames) {
  avg_frames_ = frames == 0 ? 1 : frames;
  avg_seeded_ = false;
  std::fill(avg_power_.begin(), avg_power_.end(), 0.0f);
}

}  // namespace radio::dsp
