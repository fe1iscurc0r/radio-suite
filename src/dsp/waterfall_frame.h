// SPDX-License-Identifier: AGPL-3.0-or-later
// radio-suite · dsp/waterfall_frame.h — 瀑布显示帧处理（EMA / 峰值保持 / 噪声底）
#pragma once

#include <cstdint>
#include <vector>

namespace radio::dsp {

// 输入 dB 谱，输出 8bit 显示行；同时维护三条辅助轨迹（M2 频谱线叠加用）。
// 热路径零分配：所有缓冲构造期预分配。
class WaterfallProcessor {
 public:
  explicit WaterfallProcessor(size_t bins);

  // power_db: bins 个 dB 值；out_row: bins 字节（0..255），不得与输入重叠。
  void process(const float* power_db, size_t bins, uint8_t* out_row);

  // 显示与轨迹参数（可在任意时刻改）
  float db_floor = -120.0f;
  float db_ceil = 0.0f;
  float ema_alpha = 0.6f;          // 帧间 EMA
  float peak_decay_db = 0.25f;     // 峰值保持每帧衰减
  float noise_floor_alpha = 0.02f; // 噪声底上行跟踪（下行更快，见实现）

  const std::vector<float>& emaDb() const { return ema_; }
  const std::vector<float>& peakHoldDb() const { return peak_; }
  const std::vector<float>& noiseFloorDb() const { return floor_; }
  void reset();

 private:
  std::vector<float> ema_, peak_, floor_;
  bool primed_ = false;
};

}  // namespace radio::dsp
