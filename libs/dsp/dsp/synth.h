// SPDX-License-Identifier: AGPL-3.0-or-later
// radio-suite · dsp/synth.h — 合成信号源（spike / bench / 演示：多音 + 漂移 + 噪底）
#pragma once

#include <cstdint>
#include <random>
#include <vector>

namespace radio::dsp {

class SignalSynth {
 public:
  struct Tone {
    double hz;              // 中心频率
    float amp;              // 线性幅度
    double drift_hz = 0.0;  // 慢漂移幅度（±），0 = 固定音
  };

  SignalSynth(size_t block, double sample_rate);

  void setTones(std::vector<Tone> tones);   // 相位连续替换
  void setNoiseFloor(float amp) { noise_amp_ = amp; }
  void setSampleRate(double fs);

  // 漂移周期（秒）：漂移按 sin(2π·t/T) 摆动
  void setDriftPeriod(double seconds) { drift_period_s_ = seconds; }

  // 填 block 个样本；跨调用相位/时间连续；热路径零分配。
  void fill(float* out);

  size_t block() const { return block_; }

 private:
  size_t block_;
  double fs_;
  std::vector<Tone> tones_;
  std::vector<double> phase_;
  double t_sec_ = 0.0;      // 漂移用的连续时间
  double drift_period_s_ = 9.0;
  float noise_amp_ = 2e-4f;
  std::mt19937 rng_{0xC0FFEEu};
  std::uniform_real_distribution<float> noise_{-1.0f, 1.0f};
};

}  // namespace radio::dsp
