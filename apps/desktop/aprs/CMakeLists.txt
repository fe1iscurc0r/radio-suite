# SPDX-License-Identifier: AGPL-3.0-or-later
# radio_aprs：APRS 协议核心（纯标准库，无 Qt）——解析/生成、过滤器、AX.25/KISS、AFSK、iGate
add_library(radio_aprs STATIC
  aprs.cpp
  store.cpp
  igate.cpp
  afsk.cpp)

add_library(radio::aprs ALIAS radio_aprs)

target_compile_features(radio_aprs PUBLIC cxx_std_20)
target_include_directories(radio_aprs PUBLIC
  "${CMAKE_SOURCE_DIR}/apps/desktop")

# 持久化独立成库：依赖 Qt6::Sql，sql 缺席时核心 APRS 仍可用
find_package(Qt6 QUIET COMPONENTS Sql)
if(Qt6_FOUND AND Qt6Sql_FOUND)
  add_library(radio_aprs_db STATIC persist.cpp)
  add_library(radio::aprs_db ALIAS radio_aprs_db)
  target_link_libraries(radio_aprs_db PUBLIC radio_aprs Qt6::Sql)
  target_compile_features(radio_aprs_db PUBLIC cxx_std_20)
  target_include_directories(radio_aprs_db PUBLIC
    "${CMAKE_SOURCE_DIR}/apps/desktop")
  message(STATUS "radio_aprs_db: SQLite 快照持久化启用 (Qt6::Sql)")
else()
  message(STATUS "radio_aprs_db: 未找到 Qt6::Sql，APRS 持久化禁用")
endif()
