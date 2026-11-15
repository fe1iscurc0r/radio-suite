// SPDX-License-Identifier: AGPL-3.0-or-later
// 频谱/瀑布那条轴的映射（273 调研 E 线的第一块）。
// 这里钉的是「像素→音频偏移」的算术和它的边界；轴为什么不是 RF 跨度、
// 点谱调谐为什么现在不能做，都写在 app/spectrum_axis.h 的注释里。
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "app/spectrum_axis.h"

namespace {

constexpr double kSr = 48000.0;   // 与 MainWindow::kSampleRateHz 同值
constexpr int kW = 1024;          // 一个典型的控件宽度

}  // namespace

TEST_CASE("spectrum axis maps pixels to audio offsets across the band",
          "[ui][spectrum][axis]") {
  using radio::app::columnToAudioHz;
  CHECK(columnToAudioHz(0, kW, kSr) == 0.0);
  CHECK(columnToAudioHz(kW - 1, kW, kSr) == 24000.0);
  CHECK(columnToAudioHz((kW - 1) / 2, kW, kSr) == Catch::Approx(12000.0).epsilon(0.001));
  // 单调：往右走读数必须变大，否则刻度与光标会互相打脸
  double prev = -1.0;
  for (int x = 0; x < kW; ++x) {
    const double hz = columnToAudioHz(x, kW, kSr);
    CHECK(hz >= prev);
    prev = hz;
  }
}

TEST_CASE("spectrum axis clamps out-of-range pixels and refuses bogus geometry",
          "[ui][spectrum][axis]") {
  using radio::app::columnToAudioHz;
  // 鼠标事件在控件外/布局未完成时确实会给出越界坐标
  CHECK(columnToAudioHz(-500, kW, kSr) == 0.0);
  CHECK(columnToAudioHz(kW + 500, kW, kSr) == 24000.0);
  // 还没布局好 / 采样率没给：回 0，别编一个看起来很像读数的数
  CHECK(columnToAudioHz(500, 1, kSr) == 0.0);
  CHECK(columnToAudioHz(500, 0, kSr) == 0.0);
  CHECK(columnToAudioHz(500, kW, 0.0) == 0.0);
  CHECK(columnToAudioHz(500, kW, -48000.0) == 0.0);
}

TEST_CASE("spectrum axis never claims more than Nyquist", "[ui][spectrum][axis]") {
  using radio::app::columnToAudioHz;
  // 这条断言是给「以后有人想把音频偏移当 RF 跨度去设频」准备的护栏：
  // 映射的上界永远是 fs/2，不是任何 RF 带宽
  for (const double sr : {8000.0, 48000.0, 192000.0}) {
    for (int x : {0, 1, kW / 2, kW - 2, kW - 1, kW * 3}) {
      const double hz = columnToAudioHz(x, kW, sr);
      CHECK(hz >= 0.0);
      CHECK(hz <= sr / 2.0);
    }
  }
}
