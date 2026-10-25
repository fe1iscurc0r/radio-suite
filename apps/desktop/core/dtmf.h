// SPDX-License-Identifier: AGPL-3.0-or-later
// radio-suite · core/dtmf.h — DTMF 双音多频（ITU Q.23：4 行 × 4 列）
// 编码（双音生成）+ 解码（Goertzel 8 频打分）；回环单测互锁。
// Icom 侧 DTMF 记忆命令码待 M3 对码后接入，当前用于音频生成/检测。
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace radio::core::dtmf {

// 合法键：1234567890*#ABCD
bool isDigit(char c);
std::pair<double, double> freqsOf(char c);  // {行频, 列频}

// 一串按键 → 双音 PCM（默认 80ms 音 + 40ms 静息）
std::vector<int16_t> encode(const std::string& digits,
                            uint32_t sample_rate = 44100,
                            double tone_ms = 80.0, double gap_ms = 40.0);

// 从 PCM 检出按键序列（能量门限 + 最短时长），静音/噪声返回空串
std::string decode(const std::vector<int16_t>& pcm, uint32_t sample_rate = 44100);

}  // namespace radio::core::dtmf
