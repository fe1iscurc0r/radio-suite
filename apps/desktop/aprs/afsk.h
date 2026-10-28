// SPDX-License-Identifier: AGPL-3.0-or-later
// radio-suite · aprs/afsk.h — 1200 Bd Bell 202 AFSK（HDLC 成帧 + NRZI）
//
// 发送：字节 → FCS → 位填充 → NRZI → 连续相位正弦；接收：正交相关判决 →
// 过零重同步的比特时钟 → HDLC 去填充 → CRC 校验。用于把 AX.25 包送进
// 50003 音频链（或声卡），对接 Dire Wolf / 硬件 TNC 之外的纯软件通路。
#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace radio::aprs {

struct AfskConfig {
  uint32_t sample_rate = 48000;
  double baud = 1200.0;
  double mark_hz = 1200.0;
  double space_hz = 2200.0;
};

// 返回 [-1,1] 的音频；frame 为 AX.25 字节（不含标志/FCS）
std::vector<float> afskModulate(std::span<const uint8_t> frame,
                                const AfskConfig& cfg = {},
                                int preamble_flags = 32,
                                int tail_flags = 2);

class AfskDemodulator {
 public:
  explicit AfskDemodulator(AfskConfig cfg = {});
  void feed(std::span<const int16_t> pcm);
  bool pop(std::vector<uint8_t>* frame);
  void reset();
  size_t frames() const { return frames_; }
  size_t crc_errors() const { return crc_errors_; }
  size_t overflows() const { return overflows_; }
  size_t tones() const { return tones_; }
  size_t bits() const { return bits_in_; }
  size_t flags() const { return flags_; }

 private:
  void onTone(bool mark, int64_t sample_index);
  void onBit(int bit);
  void onFlag();
  void emitFrame();

  AfskConfig cfg_;
  double bit_samples_;
  double sample_at_;
  double next_sample_;

  std::vector<float> cos_mark_, sin_mark_, cos_space_, sin_space_;
  std::vector<float> window_;
  size_t win_pos_ = 0;
  size_t win_filled_ = 0;
  double prev_disc_ = 0.0;
  bool have_disc_ = false;
  int64_t sample_index_ = 0;

  bool locked_ = false;
  int64_t next_sample_index_ = -1;
  int64_t last_tone_sample_ = -1000000;
  bool have_tone_ = false;
  bool last_tone_ = false;

  std::vector<uint8_t> bits_;
  uint8_t flag_shift_ = 0;
  int flag_shift_count_ = 0;

  std::vector<std::vector<uint8_t>> ready_;
  size_t frames_ = 0;
  size_t crc_errors_ = 0;
  size_t overflows_ = 0;
  size_t tones_ = 0;
  size_t bits_in_ = 0;
  size_t flags_ = 0;
};

}  // namespace radio::aprs
