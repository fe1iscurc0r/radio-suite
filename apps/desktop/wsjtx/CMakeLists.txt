# SPDX-License-Identifier: AGPL-3.0-or-later
# radio_wsjtx：WSJT-X UDP 消息协议（schema 3，纯标准库）
add_library(radio_wsjtx STATIC wsjtx.cpp)

add_library(radio::wsjtx ALIAS radio_wsjtx)

target_compile_features(radio_wsjtx PUBLIC cxx_std_20)
target_include_directories(radio_wsjtx PUBLIC
  "${CMAKE_SOURCE_DIR}/apps/desktop")
