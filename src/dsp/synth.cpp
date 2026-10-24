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
  reseedPhases();
}

void SignalSynth::setSampleRate(double fs) {
  if (fs <= 0.0) throw std::invalid_argument("fs must be > 0");
  fs_ = fs;
}

void SignalSynth::reseedPhases() {
  phase_.assign(tones_.size(), 0.0);
  // 各音随机初相，避免同相叠加出虚峰
  std::uniform_real_distribution<double> ph(0.0, kTwoPi);
  for (auto& p : phase_) p = ph(rng_);
}

void SignalSynth::fill(float* out) {
  for (size_t t = 0; t < tones_.size(); ++t) {
    const double w = kTwoPi * tones_[t].first / fs_;
    const double amp = static_cast<double>(tones_[t].second);
    double& ph = phase_[t];
    for (size_t i = 0; i < block_; ++i) {
      const double v = amp * std::sin(ph);
      out[i] = (t == 0) ? static_cast<float>(v) : out[i] + static_cast<float>(v);
      ph += w;
    }
    ph = std::fmod(ph, kTwoPi);
  }
  if (tones_.empty()) std::fill(out, out + block_, 0.0f);
  for (size_t i = 0; i < block_; ++i) out[i] += noise_amp_ * noise_(rng_);
}

}  // namespace radio::dsp
