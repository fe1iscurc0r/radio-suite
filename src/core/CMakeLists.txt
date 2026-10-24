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
  transport_mock.cpp)

if(RADIO_SUITE_UDP)
  find_package(Qt6 QUIET COMPONENTS Network)
  if(Qt6_FOUND)
    list(APPEND RADIO_CORE_SRC transport_udp.cpp)
    set(RADIO_CORE_EXTRA_LIBS Qt6::Network)
    set(RADIO_CORE_DEFINES RADIO_SUITE_HAVE_UDP=1)
    message(STATUS "radio_core: UDP 真机链路启用 (Qt6::Network)")
  else()
    message(STATUS "radio_core: 未找到 Qt6::Network，UDP 真机链路禁用（仅 mock）")
  endif()
endif()

add_library(radio_core STATIC ${RADIO_CORE_SRC})

add_library(radio::core ALIAS radio_core)

target_compile_features(radio_core PUBLIC cxx_std_20)
target_include_directories(radio_core PUBLIC "${CMAKE_SOURCE_DIR}/src")
if(RADIO_CORE_DEFINES)
  target_compile_definitions(radio_core PUBLIC ${RADIO_CORE_DEFINES})
endif()
if(RADIO_CORE_EXTRA_LIBS)
  target_link_libraries(radio_core PUBLIC ${RADIO_CORE_EXTRA_LIBS})
endif()
