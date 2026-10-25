// SPDX-License-Identifier: AGPL-3.0-or-later
#include "dsp/waterfall_frame.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace radio::dsp {

WaterfallProcessor::WaterfallProcessor(size_t bins)
    : ema_(bins, -200.0f), peak_(bins, -200.0f), floor_(bins, -200.0f) {
  if (bins == 0) throw std::invalid_argument("bins must be > 0");
}

void WaterfallProcessor::reset() {
  std::fill(ema_.begin(), ema_.end(), -200.0f);
  std::fill(peak_.begin(), peak_.end(), -200.0f);
  std::fill(floor_.begin(), floor_.end(), -200.0f);
  primed_ = false;
}

void WaterfallProcessor::process(const float* power_db, size_t bins, uint8_t* out_row) {
  if (bins != ema_.size()) throw std::invalid_argument("bins changed since ctor");

  const float span = db_ceil - db_floor;
  const float inv_span = span > 0.0f ? 255.0f / span : 0.0f;

  if (!primed_) {
    // 首帧直接初始化三条轨迹，避免从 -200 缓慢爬升
    for (size_t i = 0; i < bins; ++i) {
      ema_[i] = power_db[i];
      peak_[i] = power_db[i];
      floor_[i] = power_db[i] - 6.0f;
    }
    primed_ = true;
  }

  for (size_t i = 0; i < bins; ++i) {
    const float x = power_db[i];
    ema_[i] = ema_alpha * x + (1.0f - ema_alpha) * ema_[i];
    peak_[i] = std::max(x, peak_[i] - peak_decay_db);
    // 噪声底：向下快（alpha×15），向上慢（alpha）——先验：底是缓变量
    if (x < floor_[i])
      floor_[i] += noise_floor_alpha * 15.0f * (x - floor_[i]);
    else
      floor_[i] += noise_floor_alpha * 0.05f * (x - floor_[i]);

    float v = (ema_[i] - db_floor) * inv_span;
    v = std::clamp(v, 0.0f, 255.0f);
    out_row[i] = static_cast<uint8_t>(v);
  }
}

}  // namespace radio::dsp
