// SPDX-License-Identifier: AGPL-3.0-or-later
// radio-suite · ft8/ft8_codec.h — 本地 FT8 编解码（vendor: third_party/ft8_lib）
//
// 不依赖 Qt、不依赖 WSJT-X 进程：给 12 kHz 单声道 float 音频就能出解码行，
// 便于 Android/headless 复用。解调延迟按 15 s 槽计算（FT8 标准时隙）。
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace radio::ft8 {

inline constexpr int kSampleRateHz = 12000;
inline constexpr int kTones = 79;  // FT8_NN

struct DecodedMessage {
  std::string text;      // 解包后的文本（如 "CQ BG5GXO PM01"）
  float snr_db = 0.0f;   // 由 sync score 粗估（上游 demo 同式：score * 0.5）
  float dt_s = 0.0f;     // 相对槽起点的时偏
  float df_hz = 0.0f;    // 音频频偏（绝对频率，Hz）
  int score = 0;         // sync 得分，越大越可信
};

struct DecodeOptions {
  float f_min = 200.0f;       // 分析频段下界（Hz）
  float f_max = 3000.0f;      // 分析频段上界（Hz）
  int time_osr = 2;           // 时间过采样
  int freq_osr = 2;           // 频率过采样
  int min_score = 10;         // sync 门限
  int max_candidates = 140;   // 每槽候选上限
  int ldpc_iterations = 25;   // LDPC 迭代上限
};

// 输入：kSampleRateHz 单声道样本（15 s 槽 = 180000 样本）；不足也能解，成功率下降
std::vector<DecodedMessage> decode(const float* samples, size_t count,
                                   const DecodeOptions& opts = {});

// 文本 → 79 个音调（0..7）。无法按 FT8 规则打包时抛 std::runtime_error。
std::vector<uint8_t> encodeTones(const std::string& text);

// 音调 → 15 s GFSK 音频（槽内居中，两端补静音）。
std::vector<float> synthesize(const std::vector<uint8_t>& tones, float f0_hz);

}  // namespace radio::ft8
