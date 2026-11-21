// SPDX-License-Identifier: AGPL-3.0-or-later
// radio-suite · dsp/wave_io.h — 48k s16 mono WAV 录制/回放（WO-275 L3 W3.2）
//
// 口径（总纲 §6.2）：录制带头部（采样率/时标/来源链路/电台标称频率——WAV 头
// 只能承载采样率，其余记在同名 .meta 文本里）；回放吃同一套解码链，联调不依赖
// 电磁环境。采样率在头里对不上 → 拒读报错，绝不静默按 48k 硬解（A-3.5）。
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace radio::dsp {

// 写 44 字节标准 WAV 头 + s16 mono 数据。返回 false = 打不开文件（error 给原因）。
bool writeWavMono16(const std::string& path, const std::vector<int16_t>& pcm,
                    uint32_t sample_rate, std::string* error = nullptr);

// 读 WAV（s16 mono PCM）。采样率 ≠ expected_rate → 拒读（error 说明）。
// 非 PCM/非 mono/非 16bit → 拒读。压缩格式一律拒读。
bool readWavMono16(const std::string& path, uint32_t expected_rate,
                   std::vector<int16_t>* pcm, std::string* error = nullptr);

}  // namespace radio::dsp
