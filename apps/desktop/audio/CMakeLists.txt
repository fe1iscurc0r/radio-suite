# SPDX-License-Identifier: AGPL-3.0-or-later
# libradio_audio：音频输出（SSTV/DV 播放 → 电台 MOD）
# 桌面 = RtAudio (MIT, vendor)；Android = NDK AAudio（RtAudio 6.0.1 与上游 master
# 都没有 Android 后端，见 docs/HANDOVER.md §6.1）
set(_radio_audio_src audio_out.cpp audio_in.cpp
    "${CMAKE_SOURCE_DIR}/vendor/rtaudio/RtAudio.cpp")
if(ANDROID)
  set(_radio_audio_src audio_out_android.cpp audio_in_android.cpp)
endif()

add_library(radio_audio STATIC ${_radio_audio_src})

add_library(radio::audio ALIAS radio_audio)

target_compile_features(radio_audio PUBLIC cxx_std_20)
target_include_directories(radio_audio PUBLIC
  "${CMAKE_SOURCE_DIR}/apps/desktop"
  "${CMAKE_SOURCE_DIR}/vendor/rtaudio")

# 平台后端：Windows=WASAPI / Linux=ALSA / macOS=CoreAudio / Android=AAudio(API 26+)
# （此前非 Windows 一律编 ALSA，macOS 因缺 asoundlib.h 构建失败——CI 实证）
if(ANDROID)
  # AAudio 自 API 26 起提供；ANDROID_PLATFORM 形如 "android-26"
  set(_radio_api "${ANDROID_PLATFORM}")
  if(NOT _radio_api AND DEFINED ANDROID_NATIVE_API_LEVEL)
    set(_radio_api "android-${ANDROID_NATIVE_API_LEVEL}")
  endif()
  string(REGEX MATCH "([0-9]+)$" _radio_api_num "${_radio_api}")
  if(NOT _radio_api_num OR _radio_api_num LESS 26)
    message(FATAL_ERROR
      "radio_audio: Android 端用 AAudio，需要 ANDROID_PLATFORM >= android-26（当前 '${_radio_api}'）")
  endif()
  target_link_libraries(radio_audio PUBLIC aaudio android log)
elseif(WIN32)
  target_compile_definitions(radio_audio PRIVATE __WINDOWS_WASAPI__)
  target_link_libraries(radio_audio PUBLIC ole32)
elseif(APPLE)
  target_compile_definitions(radio_audio PRIVATE __MACOSX__)
  target_link_libraries(radio_audio PUBLIC
    "-framework CoreAudio" "-framework AudioToolbox" "-framework AudioUnit")
else()
  target_compile_definitions(radio_audio PRIVATE __LINUX_ALSA__)
  target_link_libraries(radio_audio PUBLIC asound pthread)
endif()

# 音频输入链冒烟工具（桌面专用：录 2s 麦克风 → 统计 20ms 帧数与电平）
if(NOT ANDROID)
  add_executable(audio-in-smoke audio_in_smoke.cpp)
  target_link_libraries(audio-in-smoke PRIVATE radio_audio)
endif()
