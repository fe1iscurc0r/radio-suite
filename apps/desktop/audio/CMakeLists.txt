# SPDX-License-Identifier: AGPL-3.0-or-later
# libradio_audio：音频输出（SSTV/DV 播放 → 电台 MOD），RtAudio (MIT) 已 vendor
add_library(radio_audio STATIC
  audio_out.cpp
  "${CMAKE_SOURCE_DIR}/vendor/rtaudio/RtAudio.cpp")

add_library(radio::audio ALIAS radio_audio)

target_compile_features(radio_audio PUBLIC cxx_std_20)
target_include_directories(radio_audio PUBLIC
  "${CMAKE_SOURCE_DIR}/apps/desktop"
  "${CMAKE_SOURCE_DIR}/vendor/rtaudio")

# WASAPI（Windows 默认）；CI Linux 无 ASIO/JACK 依赖
if(WIN32)
  target_compile_definitions(radio_audio PRIVATE __WINDOWS_WASAPI__)
  target_link_libraries(radio_audio PUBLIC ole32)
else()
  target_compile_definitions(radio_audio PRIVATE __LINUX_ALSA__)
  target_link_libraries(radio_audio PUBLIC asound pthread)
endif()
