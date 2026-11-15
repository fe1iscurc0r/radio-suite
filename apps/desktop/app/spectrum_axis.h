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
#pragma once

#include <algorithm>

namespace radio::app {

// 左边缘 = 0 Hz，右边缘 = 奈奎斯特；越界钳到两端（负数或超过半采样率不该出现）。
// width<=1 时返回 0：控件还没布局好，别编出一个看起来很像读数的数。
inline double columnToAudioHz(int x, int width, double sample_rate_hz) {
  if (width <= 1 || sample_rate_hz <= 0.0) return 0.0;
  const double frac =
      static_cast<double>(std::clamp(x, 0, width - 1)) / (width - 1);
  return frac * (sample_rate_hz / 2.0);
}

}  // namespace radio::app
