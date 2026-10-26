# SPDX-License-Identifier: AGPL-3.0-or-later
# libradio_audio：音频输出（SSTV/DV 播放 → 电台 MOD），RtAudio (MIT) 已 vendor
add_library(radio_audio STATIC
  audio_out.cpp
  audio_in.cpp
  "${CMAKE_SOURCE_DIR}/vendor/rtaudio/RtAudio.cpp")

add_library(radio::audio ALIAS radio_audio)

target_compile_features(radio_audio PUBLIC cxx_std_20)
target_include_directories(radio_audio PUBLIC
  "${CMAKE_SOURCE_DIR}/apps/desktop"
  "${CMAKE_SOURCE_DIR}/vendor/rtaudio")

# 平台后端：Windows=WASAPI / Linux=ALSA / macOS=CoreAudio
# （此前非 Windows 一律编 ALSA，macOS 因缺 asoundlib.h 构建失败——CI 实证）
if(WIN32)
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

# 音频输入链冒烟工具：录 2s 麦克风 → 统计 20ms 帧数与电平（本机验证采集）
add_executable(audio-in-smoke audio_in_smoke.cpp)
target_link_libraries(audio-in-smoke PRIVATE radio_audio)
