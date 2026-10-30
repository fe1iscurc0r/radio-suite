// SPDX-License-Identifier: AGPL-3.0-or-later
// radio-suite · app/smeter_scale.h — S 表标定（纯数学，UI 与测试共用）
//
// IARU/原厂口径：S9 = -73 dBm，S1..S9 每档 6 dB（-121..-73），
// S9 以上按 +10..+60 dB（-63..-13）。模拟表盘上 S 刻度占前 2/3 弧，
// +dB 占后 1/3——刻度位置、指针、数字读数三者必须用同一套映射。
#pragma once

#include <algorithm>

namespace radio::app {

inline constexpr float kSmeterS1Dbm = -121.0f;
inline constexpr float kSmeterS9Dbm = -73.0f;
inline constexpr float kSmeterPlus60Dbm = -13.0f;
inline constexpr float kSmeterS9Norm = 2.0f / 3.0f;
inline constexpr float kSmeterDbPerSUnit = 6.0f;

inline float smeterDbmToNorm(float dbm) {
  if (dbm <= kSmeterS9Dbm)
    return std::clamp((dbm - kSmeterS1Dbm) / (kSmeterS9Dbm - kSmeterS1Dbm),
                      0.0f, 1.0f) *
           kSmeterS9Norm;
  return kSmeterS9Norm +
         std::clamp(
             (dbm - kSmeterS9Dbm) / (kSmeterPlus60Dbm - kSmeterS9Dbm), 0.0f,
             1.0f) *
             (1.0f - kSmeterS9Norm);
}

inline float smeterNormToDbm(float norm) {
  const float n = std::clamp(norm, 0.0f, 1.0f);
  if (n <= kSmeterS9Norm)
    return kSmeterS1Dbm +
           n / kSmeterS9Norm * (kSmeterS9Dbm - kSmeterS1Dbm);
  return kSmeterS9Dbm +
         (n - kSmeterS9Norm) / (1.0f - kSmeterS9Norm) *
             (kSmeterPlus60Dbm - kSmeterS9Dbm);
}

// S 单位读数：S9 以下 1.0..9.0（小数），S9 以上返回 9.0，另用 dB 表示超出量
inline float smeterUnits(float dbm) {
  const float units = 9.0f + (dbm - kSmeterS9Dbm) / kSmeterDbPerSUnit;
  return std::clamp(units, 0.0f, 9.0f);
}

}  // namespace radio::app
