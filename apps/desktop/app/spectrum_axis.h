// SPDX-License-Identifier: AGPL-3.0-or-later
// radio-suite · app/spectrum_axis.h — 频谱/瀑布那条轴到底是什么（纯函数，谁都能测）
//
// 事实：WaterfallWidget 画的是 **RX 音频带**的 FFT——列 0 = 0 Hz，最右 = 奈奎斯特
// （48 kHz 采样 → 24 kHz）。它不是 RF 频谱，也没有中心频率：本仓现在没有任何
// RF 跨度数据源（IC-705 的 CI-V 没有谱数据流，RS-BA1 只给 48 kHz 音频）。
//
// 所以「瀑布图即调谐」（273 调研 E 线）里 *像素→RF Hz* 那一半现在做不了——
// 硬做就是拿音频偏移冒充 RF 跨度，把用户骗得很舒服。这里只给能诚实成立的那一半：
// 像素→音频偏移，用来给刻度与光标读数。inline 放头文件也是故意的：
// 纯数学不该为了测一把而把 GL 控件拖进测试目标。
//
// ⚠️ 缩放窗的算术**也**归这里管（clampZoomWindow）。它以前只在
// WaterfallWidget::setZoom 里手写，于是「被测的那份」和「在跑的那份」是两套：
// 手写的版本会把窗推出 [0,1]，刻度因此能显示 24.07k 甚至 -0.07k，而单测一直以为
// 上界永远是 fs/2。口径只能有一个，而且得是带测试的那个。
#pragma once

#include <algorithm>
#include <utility>

namespace radio::app {

// 左边缘 = 0 Hz，右边缘 = 奈奎斯特；越界钳到两端（负数或超过半采样率不该出现）。
// width<=1 时返回 0：控件还没布局好，别编出一个看起来很像读数的数。
inline double columnToAudioHz(int x, int width, double sample_rate_hz) {
  if (width <= 1 || sample_rate_hz <= 0.0) return 0.0;
  const double frac =
      static_cast<double>(std::clamp(x, 0, width - 1)) / (width - 1);
  return frac * (sample_rate_hz / 2.0);
}

// 缩放窗内 Same thing：窗 [zoom_lo, zoom_hi] ⊂ [0,1] 决定这条轴显示半带的哪一段。
// 结果照样钳在 [0, 奈奎斯特]——缩放窗本身被别处算错时，刻度也不许报出带外的数。
inline double columnToAudioHz(int x, int width, double sample_rate_hz,
                              double zoom_lo, double zoom_hi) {
  if (width <= 1 || sample_rate_hz <= 0.0) return 0.0;
  const double frac =
      static_cast<double>(std::clamp(x, 0, width - 1)) / (width - 1);
  const double lo = std::clamp(zoom_lo, 0.0, 1.0);
  const double hi = std::clamp(zoom_hi, lo, 1.0);
  const double nyquist = sample_rate_hz / 2.0;
  return std::clamp((lo + frac * (hi - lo)) * nyquist, 0.0, nyquist);
}

// 把用户给的缩放窗修成合法形状：落在 [0,1] 内、且跨度不小于 min_span
// （再窄就没有信息量了）。窄窗靠边时**平移**而不是外扩——这就是原来那个 bug 的根：
// 旧代码在 0.99..1.00 这种请求上算出 0.987..1.003，右半边直接越出奈奎斯特。
inline std::pair<double, double> clampZoomWindow(double lo, double hi,
                                                 double min_span) {
  lo = std::clamp(lo, 0.0, 1.0);
  hi = std::clamp(hi, 0.0, 1.0);
  if (min_span <= 0.0 || min_span >= 1.0) return {lo, hi};
  if (hi - lo >= min_span) return {lo, hi};
  const double c = (lo + hi) / 2.0;
  const double new_lo = std::clamp(c - min_span / 2.0, 0.0, 1.0 - min_span);
  return {new_lo, new_lo + min_span};
}

}  // namespace radio::app
