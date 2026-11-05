# SPDX-License-Identifier: AGPL-3.0-or-later
# libradio_core：CI-V 协议、频段白名单、安全门禁、状态单源、传输层
# 基础层（civ/bands/safety/state/mock）纯标准库；UDP 真机链路可选（Qt6::Network）
option(RADIO_SUITE_UDP "构建 RS-BA1 UDP 真机链路 (需要 Qt6::Network)" ON)

set(RADIO_CORE_SRC
  civ.cpp
  bands.cpp
  safety.cpp
  state.cpp
  rsba1_proto.cpp
  sstv.cpp
  dtmf.cpp
  audio_stream.cpp
  transport_mock.cpp
  # 串口 CI-V 的帧定界/重同步部分：不碰 QSerialPort，所以缺 SerialPort 的套件
  # 也照样编译、照样用注入的内存管道跑用例（真后端见下面的 _qt TU）。
  transport_serial.cpp)

if(RADIO_SUITE_UDP)
  find_package(Qt6 QUIET COMPONENTS Network)
  if(Qt6_FOUND)
    list(APPEND RADIO_CORE_SRC transport_udp.cpp transport_pybridge.cpp
         transport_tcp.cpp transport_tcp_gateway.cpp)
    list(APPEND RADIO_CORE_EXTRA_LIBS Qt6::Network)
    list(APPEND RADIO_CORE_DEFINES RADIO_SUITE_HAVE_UDP=1)
    message(STATUS "radio_core: UDP 真机链路 + rsba1-core Python 桥启用 (Qt6::Network)")
  else()
    message(STATUS "radio_core: 未找到 Qt6::Network，UDP/桥禁用（仅 mock）")
  endif()
endif()

# 串口对端（A3）：CI-V over CAT 线。桌面/树莓派有 QtSerialPort 就出厂，
# Android 的 Qt 套件没有 SerialPort —— 那正是「手机经 TCP 连 PC 网关」这条路
# 存在的原因，网关工具靠这个后端驱动真电台。
find_package(Qt6SerialPort QUIET)
if(TARGET Qt6::SerialPort)
  list(APPEND RADIO_CORE_SRC transport_serial_qt.cpp)
  list(APPEND RADIO_CORE_EXTRA_LIBS Qt6::SerialPort)
  list(APPEND RADIO_CORE_DEFINES RADIO_SUITE_HAVE_SERIAL_QT=1)
  message(STATUS "radio_core: CI-V 串口后端启用 (Qt6::SerialPort)")
else()
  message(STATUS "radio_core: 没有 Qt6::SerialPort，串口 CI-V 只留帧逻辑（注入管道可测），真串口不可用")
endif()

add_library(radio_core STATIC ${RADIO_CORE_SRC})

add_library(radio::core ALIAS radio_core)

target_compile_features(radio_core PUBLIC cxx_std_20)
target_include_directories(radio_core PUBLIC
  "${CMAKE_SOURCE_DIR}/apps/desktop"
  "${CMAKE_SOURCE_DIR}/libs")
if(RADIO_CORE_DEFINES)
  target_compile_definitions(radio_core PUBLIC ${RADIO_CORE_DEFINES})
endif()
if(RADIO_CORE_EXTRA_LIBS)
  target_link_libraries(radio_core PUBLIC ${RADIO_CORE_EXTRA_LIBS})
endif()
