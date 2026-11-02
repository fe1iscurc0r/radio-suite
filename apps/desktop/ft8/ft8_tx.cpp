// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ft8/ft8_tx.h"

#include <algorithm>
#include <cmath>

#include "ft8/ft8_codec.h"

namespace radio::ft8 {

std::vector<int16_t> renderTxPcm(const std::string& text, float f0_hz,
                                 float peak) {
  const auto tones = encodeTones(text);
  const auto audio = synthesize(tones, f0_hz);  // 12 kHz，正好一槽
  // 零阶保持 4× 上采样到 48 kHz：解码侧按 4 样本求和平均还原，往返无损
  std::vector<int16_t> pcm(audio.size() * 4);
  const float scale =
      std::clamp(peak, 0.0f, 1.0f) * 32767.0f;
  for (size_t i = 0; i < audio.size(); ++i) {
    const auto s = static_cast<int16_t>(std::lround(audio[i] * scale));
    for (int k = 0; k < 4; ++k) pcm[i * 4 + k] = s;
  }
  return pcm;
}

int64_t nextTxSlotStart(int64_t now_utc_ms, int64_t min_lead_ms, int parity) {
  const int64_t earliest = now_utc_ms + std::max<int64_t>(min_lead_ms, 0);
  int64_t start =
      (earliest + kTxSlotMs - 1) / kTxSlotMs * kTxSlotMs;  // 向上取整到槽边界
  if (parity == 0 || parity == 1) {
    if ((start / kTxSlotMs) % 2 != parity) start += kTxSlotMs;
  }
  return start;
}

}  // namespace radio::ft8
