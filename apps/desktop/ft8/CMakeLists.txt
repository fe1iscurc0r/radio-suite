# SPDX-License-Identifier: AGPL-3.0-or-later
# 本地 FT8 编解码封装（纯标准库；解调由 third_party/ft8_lib 提供）
add_library(radio_ft8 STATIC ft8_codec.cpp ft8_slot.cpp ft8_tx.cpp)
add_library(radio::ft8_codec ALIAS radio_ft8)
target_include_directories(radio_ft8 PUBLIC "${CMAKE_SOURCE_DIR}/apps/desktop")
target_link_libraries(radio_ft8 PUBLIC radio::ft8lib)
find_package(Threads REQUIRED)
target_link_libraries(radio_ft8 PUBLIC Threads::Threads)
