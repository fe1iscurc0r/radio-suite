// SPDX-License-Identifier: AGPL-3.0-or-later
// radio-suite · ft8/ft8_tx.h — FT8 本机发射：波形渲染与时隙排期（无 Qt 依赖）
//
// 渲染走 ft8_codec 的 GFSK 合成（上游 gen_ft8 同式），时隙对齐由调用方负责：
// 本模块产出的 PCM 正好是一整个 15 s 槽、波形在槽内居中，「从槽起点开始播」即对齐。
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace radio::ft8 {

// FT8 槽长 15 s；槽序号 = UTC 毫秒 / 槽长，偶序号槽落在每分钟的 :00 与 :30
inline constexpr int64_t kTxSlotMs = 15'000;

// 文本 → 一整个 15 s 槽的 48 kHz s16 mono 发射 PCM。peak 为满量程比例，默认 0.7
// （与上游 gen_ft8 一致）。文本无法按 FT8 规则打包时抛 std::runtime_error。
std::vector<int16_t> renderTxPcm(const std::string& text, float f0_hz,
                                 float peak = 0.7f);

// 下一个可发射的槽起点（UTC 毫秒）：>= now + min_lead_ms，并按槽序号奇偶过滤
// （parity：-1 任意，0 偶槽，1 奇槽）。min_lead 留给 PTT 键控与音频设备启动。
int64_t nextTxSlotStart(int64_t now_utc_ms, int64_t min_lead_ms, int parity);

}  // namespace radio::ft8
