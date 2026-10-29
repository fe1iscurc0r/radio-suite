// SPDX-License-Identifier: AGPL-3.0-or-later
// radio-suite · dsp/fft.h — 实数 FFT 封装（pocketfft，BSD-3 header-only）
//
// 线程模型（工单）：dsp 计算线程专用；热路径零堆分配（全部缓冲构造期预分配）。
// M2 将把输出接入 ring buffer + 双缓冲；M0 直接暴露内部缓冲。
#pragma once

#include <complex>
#include <cstdint>
#include <vector>

namespace radio::dsp {

class SpectrumAnalyzer {
 public:
  // fft_size 任意 ≥8 即可（pocketfft 支持非 2 幂）；采样率用于换算 bin 宽。
  SpectrumAnalyzer(size_t fft_size, double sample_rate);

  size_t fftSize() const { return n_; }
  size_t binCount() const { return n_ / 2 + 1; }
  double binHz() const { return sample_rate_ / static_cast<double>(n_); }
  double sampleRate() const { return sample_rate_; }

  // 输入 fft_size 个实数采样；输出幂律谱（dBFS）。可重入调用，缓冲复用。
  void process(const float* samples, size_t n);

  // Welch 功率平均：1 = 关闭；k>1 时按 1/k 递归平均线性功率。
  // 噪声底起伏按 ~1/√k 收敛，相参单音幅度不变（以时间分辨率换显示稳定度）。
  void setAveragingFrames(size_t frames);
  size_t averagingFrames() const { return avg_frames_; }

  const std::vector<float>& powerDb() const { return power_db_; }
  uint64_t frameSeq() const { return seq_; }

 private:
  size_t n_;
  double sample_rate_;
  std::vector<float> window_;                        // 周期 Hann（分析窗）
  std::vector<float> windowed_;                      // 加窗后时域
  std::vector<std::complex<float>> spec_;            // r2c 输出
  std::vector<float> power_db_;                      // dB 谱
  std::vector<float> avg_power_;                     // 线性功率平均
  std::vector<size_t> shape_;                        // pocketfft 元数据（构造期定死）
  std::vector<ptrdiff_t> stride_in_, stride_out_;    // 单位：字节
  double window_sum_ = 0.0;
  size_t avg_frames_ = 1;
  bool avg_seeded_ = false;
  uint64_t seq_ = 0;
};

}  // namespace radio::dsp
