# SPDX-License-Identifier: AGPL-3.0-or-later
# radio_session：QObject 会话层——RadioSession（transport 会话 + SafetyGate 门禁编排
# + QTimer 轮询）。上承 radio_core（纯 std），下启 radio_bus 与 app（UI）。
# 头文件暴露 core 类型（transport/safety/state），故 radio::core 与 Qt6::Core 均 PUBLIC。
find_package(Qt6 REQUIRED COMPONENTS Core)

add_library(radio_session STATIC session.cpp)

add_library(radio::session ALIAS radio_session)

target_compile_features(radio_session PUBLIC cxx_std_20)
target_include_directories(radio_session PUBLIC
  "${CMAKE_SOURCE_DIR}/apps/desktop")
target_link_libraries(radio_session PUBLIC
  radio::core
  Qt6::Core)
set_target_properties(radio_session PROPERTIES AUTOMOC ON)
