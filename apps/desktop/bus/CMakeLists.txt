# SPDX-License-Identifier: AGPL-3.0-or-later
# radio_bus：本机集成总线——HTTP bus_server（Qt6::HttpServer）+ rigctld 服务器
# （QTcpServer）。命令出口就是 RadioSession，故依赖 radio_session（PUBLIC）。
# bus_server.h 还暴露 dsp/fft.h、dsp/synth.h（合成谱源），radio::dsp 也须 PUBLIC。
find_package(Qt6 REQUIRED COMPONENTS Network)
find_package(Qt6HttpServer REQUIRED)

add_library(radio_bus STATIC bus_server.cpp rigctld_server.cpp)

add_library(radio::bus ALIAS radio_bus)

target_compile_features(radio_bus PUBLIC cxx_std_20)
target_include_directories(radio_bus PUBLIC
  "${CMAKE_SOURCE_DIR}/apps/desktop")
target_link_libraries(radio_bus PUBLIC
  radio_session
  radio::dsp
  Qt6::Network
  Qt6::HttpServer)
# bus 已无条件可用，宏仍 PUBLIC 保留：MainWindow 自检报告用它标记 rig_http_bus_built
target_compile_definitions(radio_bus PUBLIC RADIO_SUITE_HAVE_BUS=1)
set_target_properties(radio_bus PROPERTIES AUTOMOC ON)
