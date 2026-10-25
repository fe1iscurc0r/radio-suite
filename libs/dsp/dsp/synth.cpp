// SPDX-License-Identifier: AGPL-3.0-or-later
#include "dsp/synth.h"

#include <cmath>
#include <stdexcept>

namespace radio::dsp {

constexpr double kTwoPi = 6.283185307179586476925286766559;

SignalSynth::SignalSynth(size_t block, double sample_rate)
    : block_(block), fs_(sample_rate) {
  if (block == 0 || fs_ <= 0.0) throw std::invalid_argument("bad synth args");
}

void SignalSynth::setTones(std::vector<Tone> tones) {
  tones_ = std::move(tones);
  phase_.assign(tones_.size(), 0.0);
  // 各音随机初相，避免同相叠加出虚峰
  std::uniform_real_distribution<double> ph(0.0, kTwoPi);
  for (auto& p : phase_) p = ph(rng_);
}

void SignalSynth::setSampleRate(double fs) {
  if (fs <= 0.0) throw std::invalid_argument("fs must be > 0");
  fs_ = fs;
}

void SignalSynth::fill(float* out) {
  const double block_sec = static_cast<double>(block_) / fs_;
  for (size_t t = 0; t < tones_.size(); ++t) {
    const Tone& tone = tones_[t];
    const double w0 = kTwoPi * tone.hz / fs_;
    const double wd = kTwoPi * tone.drift_hz / fs_;
    const double drift_w = kTwoPi / (drift_period_s_ * fs_);  // 漂移相位步进
    const double amp = static_cast<double>(tone.amp);
    double& ph = phase_[t];
    if (tone.drift_hz == 0.0) {
      for (size_t i = 0; i < block_; ++i) {
        const double v = amp * std::sin(ph);
        out[i] = (t == 0) ? static_cast<float>(v) : out[i] + static_cast<float>(v);
        ph += w0;
      }
    } else {
      // 瞬时频率 = f0 + drift·sin(2π·t/T)：相位按积分累积（无相位跳变）
      double drift_ph = kTwoPi * t_sec_ / drift_period_s_;
      for (size_t i = 0; i < block_; ++i) {
        const double v = amp * std::sin(ph);
        out[i] = (t == 0) ? static_cast<float>(v) : out[i] + static_cast<float>(v);
        ph += w0 + wd * std::sin(drift_ph);
        drift_ph += drift_w;
      }
    }
    ph = std::fmod(ph, kTwoPi);
  }
  if (tones_.empty()) std::fill(out, out + block_, 0.0f);
  for (size_t i = 0; i < block_; ++i) out[i] += noise_amp_ * noise_(rng_);
  t_sec_ += block_sec;
}

}  // namespace radio::dsp
