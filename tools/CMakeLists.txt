# 台架小工具（不依赖 Qt）：只在有 FT8 编解码 + 声卡输出的平台上建
if(NOT ANDROID AND TARGET radio::ft8_codec AND TARGET radio::audio)
  add_executable(ft8play ft8play.cpp)
  target_link_libraries(ft8play PRIVATE radio::ft8_codec radio::audio)
  target_include_directories(ft8play PRIVATE
    "${CMAKE_SOURCE_DIR}/apps/desktop")
endif()

# CI-V over TCP 网关（PC 侧）：手机没有 QtSerialPort，靠它把电台那一侧变成一条
# TCP 连接。电台侧目前只有内置 mock，所以它同时是 N2 那条真机测试的对端。
find_package(Qt6 QUIET COMPONENTS Network)
if(NOT ANDROID AND TARGET Qt6::Network)
  add_executable(civ_tcp_gateway civ_tcp_gateway.cpp)
  target_link_libraries(civ_tcp_gateway PRIVATE radio::core Qt6::Network)
  target_include_directories(civ_tcp_gateway PRIVATE
    "${CMAKE_SOURCE_DIR}/apps/desktop")
endif()

# WO-275 L1：fake_rig —— 假电台 LAN 化台架工具（受控局域网用，默认不建）。
# 开关与 BUILD_ENGINE_SPIKE 同构（默认 OFF）；链接 tests/fake_rsba1_server.cpp，
# 不进产品二进制、不进 APK（总纲 A-1.8 口径）。RADIO_SUITE_UDP=ON 才有真客户端自证。
if(RADIO_SUITE_BUILD_BENCH_TOOLS AND NOT ANDROID AND TARGET Qt6::Network
   AND RADIO_SUITE_UDP)
  add_executable(fake_rig
    fake_rig.cpp
    "${CMAKE_SOURCE_DIR}/tests/fake_rsba1_server.cpp")
  target_link_libraries(fake_rig PRIVATE radio::core Qt6::Network)
  target_include_directories(fake_rig PRIVATE
    "${CMAKE_SOURCE_DIR}/apps/desktop"
    "${CMAKE_SOURCE_DIR}/tests")
  set_target_properties(fake_rig PROPERTIES AUTOMOC ON)
endif()
