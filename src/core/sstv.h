// SPDX-License-Identifier: AGPL-3.0-or-later
// radio-suite · core/sstv.h — SSTV Robot36 编码器（纯函数，无 Qt）
//
// 规格（MMSSTV/PySSTV 通用参数表）：
//   320×240，VIS=0x08；像素 275µs；黑=1500Hz 白=2300Hz；sync=1200Hz 9ms porch=1500Hz 3ms
//   偶行：sync+porch+Y(88ms)
//   奇行：sync+porch+Y(88ms)+sync+porch+色差(88ms)，色差按行交替 R-Y / B-Y
//   VIS：1200Hz 300ms + 1100Hz 30ms + 7 位×30ms(LSB first, 1300=1/1100=0) + 1200Hz 300ms
// 自校验：tests/test_sstv.cpp 用 Goertzel 检测各段主导频率 + 总时长恒等式。
#pragma once

#include <cstdint>
#include <vector>

namespace radio::core::sstv {

struct Rgb {
  uint8_t r = 0, g = 0, b = 0;
};

// 320×240 行主序图像 → 16bit PCM 采样（幅度 ±0.8 满幅，防爆音）
std::vector<int16_t> encodeRobot36(const std::vector<Rgb>& image320x240,
                                   uint32_t sample_rate = 44100);

// 色度变换（供测试独立校验）
uint8_t toLuma(uint8_t r, uint8_t g, uint8_t b);         // Y  (BT.470)
uint8_t toChromaR(uint8_t r, uint8_t g, uint8_t b);      // R-Y (+128 截断)
uint8_t toChromaB(uint8_t r, uint8_t g, uint8_t b);      // B-Y (+128 截断)

inline constexpr double kPixelMs = 88.0 / 320.0;  // 275µs
inline constexpr uint32_t kRobot36Vis = 0x08;

}  // namespace radio::core::sstv
