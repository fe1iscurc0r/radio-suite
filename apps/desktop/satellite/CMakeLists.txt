# SPDX-License-Identifier: AGPL-3.0-or-later
# radio_satellite：过境几何 + 多普勒 + 上下行预补偿（纯标准库，传播靠 vendored libsgp4）
add_library(radio_satellite STATIC sat_track.cpp)

add_library(radio::satellite ALIAS radio_satellite)

target_compile_features(radio_satellite PUBLIC cxx_std_20)
target_include_directories(radio_satellite PUBLIC
  "${CMAKE_SOURCE_DIR}/apps/desktop")
target_link_libraries(radio_satellite PUBLIC radio::sgp4)
