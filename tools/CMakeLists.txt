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
