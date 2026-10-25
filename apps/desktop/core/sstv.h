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
#include <optional>
#include <vector>

namespace radio::core::sstv {

struct Rgb {
  uint8_t r = 0, g = 0, b = 0;
};

// SSTV 制式（录音盒模型：电脑编解码=制式自由，可编任意制式送电台 MOD）
// 制式参数与 VoX 校准头对齐上游 SSTVEncoder2（Apache-2.0， NOTICE 署名于
// docs/sstvencoder2-notice.txt；JL Barber "Dayton Paper" 规格书）
enum class Mode : uint8_t {
  Robot36,   // VIS 0x08，320×240，36s
  Robot72,   // VIS 0x0C，320×240，72s（Y+R-Y+B-Y 全行传）
  Martin1,   // VIS 0x2C(44)，320×256，~115s（GBR 顺序）
  Martin2,   // VIS 0x28(40)，320×256，~58s（M1 半速）
  Scottie1,  // VIS 60，320×256，~110s（G/B/R + 行首 sync 后置）
  Scottie2,  // VIS 56，320×256，~55s（S1 半速）
  PD180,     // VIS 96，640×496，~187s（YVUY 每 2 行，YUV440P）
  PD120,     // VIS 95，640×496，~127s（PD180 半速）
};

// 制式名（UI 下拉用）
const char* modeName(Mode m);
// VIS → 制式（解码自动识别）
std::optional<Mode> modeFromVis(uint8_t vis);
int imageWidth(Mode m);
int imageHeight(Mode m);

// 编码（图像尺寸须匹配制式）→ 16bit PCM
std::vector<int16_t> encodeSstv(Mode m, const std::vector<Rgb>& image,
                                uint32_t sample_rate = 44100);

// 解码：按 VIS 头自动识别制式；返回空 = 未识别
std::vector<Rgb> decodeSstv(const std::vector<int16_t>& pcm,
                            uint32_t sample_rate = 44100);

// 320×240 行主序图像 → 16bit PCM 采样（幅度 ±0.8 满幅，防爆音）
// 兼容旧接口：Robot36 特例（新代码用 encodeSstv/decodeSstv 多制式版）
std::vector<int16_t> encodeRobot36(const std::vector<Rgb>& image320x240,
                                   uint32_t sample_rate = 44100);

// Robot36 解码：PCM → 图像。返回空 = 未找到 VIS=8 帧头。
// 与编码器构成回环（tests/test_sstv.cpp：encode→decode 像素级比对）
std::vector<Rgb> decodeRobot36(const std::vector<int16_t>& pcm,
                               uint32_t sample_rate = 44100);

// 色度变换（供测试独立校验）
uint8_t toLuma(uint8_t r, uint8_t g, uint8_t b);         // Y  (BT.470)
uint8_t toChromaR(uint8_t r, uint8_t g, uint8_t b);      // R-Y (+128 截断)
uint8_t toChromaB(uint8_t r, uint8_t g, uint8_t b);      // B-Y (+128 截断)

inline constexpr double kPixelMs = 88.0 / 320.0;  // 275µs
inline constexpr uint32_t kRobot36Vis = 0x08;

}  // namespace radio::core::sstv
