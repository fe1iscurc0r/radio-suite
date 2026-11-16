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

TEST_CASE("spectrum axis maps inside a zoom window and matches the unzoomed case",
          "[ui][spectrum][axis]") {
  using radio::app::columnToAudioHz;
  // 满窗必须与不带窗的版本逐点一致，否则「同一个口径」是假的
  for (int x : {0, 1, kW / 4, kW / 2, kW - 1})
    CHECK(columnToAudioHz(x, kW, kSr, 0.0, 1.0) == columnToAudioHz(x, kW, kSr));

  // 窗 [0.25,0.5] = 6k..12k：左端 6k、右端 12k、正中 9k。
  // 中间的容差按「半列」给：一列 = (12000-6000)/1023 ≈ 5.9 Hz，
  // 而 (kW-1)/2 = 511 列的 frac 是 511/1023 而不是 0.5，所以留 3 Hz。
  CHECK(columnToAudioHz(0, kW, kSr, 0.25, 0.5) == Catch::Approx(6000.0).margin(1.0));
  CHECK(columnToAudioHz(kW - 1, kW, kSr, 0.25, 0.5) ==
        Catch::Approx(12000.0).margin(1.0));
  CHECK(columnToAudioHz((kW - 1) / 2, kW, kSr, 0.25, 0.5) ==
        Catch::Approx(9000.0).margin(3.0));

  // 窗内仍然单调
  double prev = -1.0;
  for (int x = 0; x < kW; ++x) {
    const double hz = columnToAudioHz(x, kW, kSr, 0.75, 1.0);
    CHECK(hz >= prev);
    prev = hz;
  }
}

TEST_CASE("spectrum axis clamps a bogus zoom window back into the band",
          "[ui][spectrum][axis]") {
  using radio::app::columnToAudioHz;
  // 刻度这行字是用户唯一能看见的映射输出：窗被算错时它也不许越带。
  // 旧代码在贴边窄窗上会算出 24.0675k / -0.0675k，靠的就是这里兜住。
  CHECK(columnToAudioHz(kW - 1, kW, kSr, 0.987, 1.003) <= 24000.0);
  CHECK(columnToAudioHz(0, kW, kSr, -0.003, 0.013) >= 0.0);
  // hi < lo 这种倒挂窗不许产生负读数
  CHECK(columnToAudioHz(kW - 1, kW, kSr, 0.8, 0.2) >= 0.0);
}

TEST_CASE("spectrum axis zoom window normalization never leaves [0,1]",
          "[ui][spectrum][axis]") {
  using radio::app::clampZoomWindow;
  constexpr double kMinSpan = 1.0 / 64.0;

  // 回归用例：贴边窄窗。旧实现「围绕中心外扩」算出 0.9871875..1.0028125，
  // 右端直接越出奈奎斯特；正确做法是平移。
  auto right = clampZoomWindow(0.99, 1.00, kMinSpan);
  CHECK(right.first >= 0.0);
  CHECK(right.second <= 1.0);
  CHECK(right.second == Catch::Approx(1.0).margin(1e-9));  // 贴右就顶到 1，别外溢
  CHECK(right.second - right.first >= kMinSpan - 1e-12);

  auto left = clampZoomWindow(0.0, 0.01, kMinSpan);
  CHECK(left.first == Catch::Approx(0.0).margin(1e-9));
  CHECK(left.second <= 1.0);
  CHECK(left.second - left.first >= kMinSpan - 1e-12);

  // 宽窗原样保留，别把用户框好的范围改掉
  auto wide = clampZoomWindow(0.2, 0.9, kMinSpan);
  CHECK(wide.first == Catch::Approx(0.2).margin(1e-9));
  CHECK(wide.second == Catch::Approx(0.9).margin(1e-9));

  // 越界请求先钳进来；倒挂窗不许产生 hi<lo
  auto bogus = clampZoomWindow(-0.5, 3.0, kMinSpan);
  CHECK(bogus.first >= 0.0);
  CHECK(bogus.second <= 1.0);
  CHECK(bogus.second >= bogus.first);

  // 全量扫一遍：任何请求窗经归一化后，整条轴的读数都必须在 [0, 奈奎斯特] 内。
  // 这条是给刻度护栏用的——滚轮、框选、双击都汇到同一个 setZoom。
  for (int i = 0; i <= 50; ++i) {
    for (int j = 0; j <= 50; ++j) {
      const double a = i / 50.0, b = j / 50.0;
      const auto w = clampZoomWindow(std::min(a, b), std::max(a, b), kMinSpan);
      REQUIRE(w.first >= 0.0);
      REQUIRE(w.second <= 1.0);
      REQUIRE(w.second >= w.first);
      for (int x : {0, kW / 2, kW - 1}) {
        const double hz = radio::app::columnToAudioHz(x, kW, kSr, w.first, w.second);
        CHECK(hz >= 0.0);
        CHECK(hz <= kSr / 2.0);
      }
    }
  }
}
