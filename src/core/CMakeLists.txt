# SPDX-License-Identifier: AGPL-3.0-or-later
# libradio_core：CI-V 协议、频段白名单、安全门禁、状态单源、传输层
# 纯标准库 C++，不依赖 Qt（M1 引入 QSerialPort/QUdpSocket 时再评估）
add_library(radio_core STATIC
  civ.cpp
  bands.cpp
  safety.cpp
  state.cpp
  transport_mock.cpp)

add_library(radio::core ALIAS radio_core)

target_compile_features(radio_core PUBLIC cxx_std_20)
target_include_directories(radio_core PUBLIC "${CMAKE_SOURCE_DIR}/src")
