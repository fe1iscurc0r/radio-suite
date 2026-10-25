# SPDX-License-Identifier: AGPL-3.0-or-later
# WO-274 B 节 spike（默认不编：-DBUILD_ENGINE_SPIKE=ON 显式开启）
# BUILD_GUI=OFF 时 OpenGLWidgets 未引入——spike 自行 find（WaterfallWidget 需要）
find_package(Qt6 6.4 REQUIRED COMPONENTS Widgets Network WebSockets OpenGLWidgets)
set(CMAKE_AUTOMOC ON)

add_executable(engine-spike
  main_spike.cpp
  engine_spike.cpp)

target_link_libraries(engine-spike PRIVATE
  Qt6::Widgets
  Qt6::Network
  Qt6::WebSockets
  Qt6::OpenGLWidgets
  radio::dsp)  # WaterfallWidget 复用（app 目标外的控件依赖 dsp+gui）
# WaterfallWidget 在 src/app 内——spike 直接编入该源文件
target_sources(engine-spike PRIVATE
  "${CMAKE_SOURCE_DIR}/apps/desktop/app/WaterfallWidget.cpp")
target_include_directories(engine-spike PRIVATE
  "${CMAKE_SOURCE_DIR}"
  "${CMAKE_SOURCE_DIR}/apps/desktop"
  "${CMAKE_SOURCE_DIR}/apps/desktop/app"
  "${CMAKE_SOURCE_DIR}/libs")
