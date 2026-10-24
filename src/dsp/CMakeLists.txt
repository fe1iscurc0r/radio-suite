# SPDX-License-Identifier: AGPL-3.0-or-later
# libradio_dsp：FFT / 瀑布帧处理 / 合成信号（纯标准库 + pocketfft，不依赖 Qt、core）
add_library(radio_dsp STATIC
  fft.cpp
  waterfall_frame.cpp
  synth.cpp)

add_library(radio::dsp ALIAS radio_dsp)

target_compile_features(radio_dsp PUBLIC cxx_std_20)
target_include_directories(radio_dsp PUBLIC
  "${CMAKE_SOURCE_DIR}/src")
target_include_directories(radio_dsp SYSTEM PUBLIC
  "${pocketfft_SOURCE_DIR}")
