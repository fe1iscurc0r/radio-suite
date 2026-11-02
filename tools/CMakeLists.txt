# 台架小工具（不依赖 Qt）：只在有 FT8 编解码 + 声卡输出的平台上建
if(NOT ANDROID AND TARGET radio::ft8_codec AND TARGET radio::audio)
  add_executable(ft8play ft8play.cpp)
  target_link_libraries(ft8play PRIVATE radio::ft8_codec radio::audio)
  target_include_directories(ft8play PRIVATE
    "${CMAKE_SOURCE_DIR}/apps/desktop")
endif()
