// SPDX-License-Identifier: AGPL-3.0-or-later
// radio-suite · dsp/synth.h — 合成信号源（spike / bench / 演示：多音 + 噪底）
#pragma once

#include <cstdint>
#include <random>
#include <utility>
#include <vector>

namespace radio::dsp {

class SignalSynth {
 public:
  using Tone = std::pair<double /*Hz*/, float /*线性幅度*/>;

  SignalSynth(size_t block, double sample_rate);

  void setTones(std::vector<Tone> tones);   // 相位连续替换
  void setNoiseFloor(float amp) { noise_amp_ = amp; }
  void setSampleRate(double fs);

  // 填 block 个样本；跨调用相位连续；热路径零分配。
  void fill(float* out);

  size_t block() const { return block_; }

 private:
  void reseedPhases();

  size_t block_;
  double fs_;
  std::vector<Tone> tones_;
  std::vector<double> phase_;
  float noise_amp_ = 2e-4f;
  std::mt19937 rng_{0xC0FFEEu};
  std::uniform_real_distribution<float> noise_{-1.0f, 1.0f};
};

}  // namespace radio::dsp
