// SPDX-License-Identifier: AGPL-3.0-or-later
// SSTV Robot36 编码器自校验：时长恒等式 + Goertzel 分段频率检测
// （兑现"时序对表自校验，不产错时序废品"）
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

#include "core/sstv.h"

using namespace radio::core;
using Catch::Approx;

namespace {

// Goertzel：在 [begin,begin+len) 采样上检测指定频率的功率
double goertzel(const std::vector<int16_t>& s, size_t begin, size_t len,
                double hz, uint32_t fs) {
  const double k = std::round(len * hz / fs);  // 标准：k = N·f/fs
  const double w = 2.0 * 3.14159265358979323846 * k / len;
  const double coeff = 2.0 * std::cos(w);
  double q0 = 0, q1 = 0, q2 = 0;
  for (size_t i = 0; i < len; ++i) {
    q0 = coeff * q1 - q2 + s[begin + i];
    q2 = q1;
    q1 = q0;
  }
  return q1 * q1 + q2 * q2 - coeff * q1 * q2;
}

double dominant(const std::vector<int16_t>& s, size_t begin, size_t len,
                uint32_t fs) {
  double best = 0, best_p = -1;
  for (double hz = 1000.0; hz <= 2400.0; hz += 25.0) {
    const double p = goertzel(s, begin, len, hz, fs);
    if (p > best_p) {
      best_p = p;
      best = hz;
    }
  }
  return best;
}

constexpr uint32_t kFs = 44100;
constexpr size_t ms(double v) { return static_cast<size_t>(v * kFs / 1000.0); }

std::vector<sstv::Rgb> solidImage(uint8_t r, uint8_t g, uint8_t b) {
  std::vector<sstv::Rgb> img(320 * 240);
  for (auto& px : img) px = {r, g, b};
  return img;
}

}  // namespace

TEST_CASE("sstv: luma/chroma transform anchors", "[sstv]") {
  CHECK(sstv::toLuma(255, 255, 255) == 255);
  CHECK(sstv::toLuma(0, 0, 0) == 0);
  CHECK(sstv::toLuma(255, 0, 0) == 76);   // 0.299×255
  CHECK(sstv::toLuma(0, 0, 255) == 29);   // 0.114×255
  CHECK(sstv::toChromaR(255, 0, 0) == 255);   // 满红 → R-Y 最大
  CHECK(sstv::toChromaB(0, 0, 255) == 255);   // 满蓝 → B-Y 最大
  CHECK(sstv::toChromaR(128, 128, 128) == Approx(128.0).margin(2));  // 灰→零差
}

TEST_CASE("sstv: Robot36 total duration identity", "[sstv]") {
  // 恒等式（独立于编码器代码推导）：
  //   VIS: 300+30+7×30+300 = 840ms
  //   120 偶行×(9+3+88) = 12000ms；120 奇行×(9+3+88+9+3+88) = 24000ms
  //   合计 36840ms ± 取整误差
  const auto wav = sstv::encodeRobot36(solidImage(128, 128, 128), kFs);
  const double total_ms = double(wav.size()) / kFs * 1000.0;
  CHECK(total_ms == Approx(36840.0).margin(30.0));
}

TEST_CASE("sstv: segment frequencies (Goertzel self-check)", "[sstv]") {
  // 黑图（1500Hz 扫描线）
  const auto black = sstv::encodeRobot36(solidImage(0, 0, 0), kFs);

  // ① VIS 起始段（300ms 纯音）主频 = 1200Hz（长窗可全频扫描）
  CHECK(dominant(black, ms(50), ms(150), kFs) == Approx(1200.0).margin(40.0));
  // ② VIS bit0 = 1100Hz（VIS=8 → LSB bit0=0）
  CHECK(dominant(black, ms(335), ms(25), kFs) == Approx(1100.0).margin(40.0));

  // 短段（sync 9ms / porch 3ms）用候选频率功率比较制（避免短窗 k 量化误判）
  auto power = [&](const std::vector<int16_t>& s, double t0, double dur_ms,
                   double hz) {
    return goertzel(s, ms(t0), ms(dur_ms), hz, kFs);
  };
  // ③ 首行 sync [840,849)：1200 应强于 1500
  CHECK(power(black, 840.5, 8.0, 1200.0) > power(black, 840.5, 8.0, 1500.0) * 4);
  // ④ 首行 porch [849,852)：1500 强于 1200
  CHECK(power(black, 849.5, 2.5, 1500.0) > power(black, 849.5, 2.5, 1200.0));
  // ⑤ 黑像素扫描 [852,892]：1500 强于 2300
  CHECK(power(black, 853.0, 35.0, 1500.0) > power(black, 853.0, 35.0, 2300.0) * 4);

  // 白图扫描线 = 2300Hz 强于 1500
  const auto white = sstv::encodeRobot36(solidImage(255, 255, 255), kFs);
  CHECK(power(white, 853.0, 35.0, 2300.0) > power(white, 853.0, 35.0, 1500.0) * 4);
}
