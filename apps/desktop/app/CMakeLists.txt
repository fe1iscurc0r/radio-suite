# SPDX-License-Identifier: AGPL-3.0-or-later
# radio-suite 可执行：Qt Widgets + QOpenGLWidget 瀑布 + HamLog 融合 + APRS 面板
# + --headless --bus HTTP 总线（M5 第一片，bus 源直接编入；全量时再独立成库）
find_package(Qt6 6.4 REQUIRED COMPONENTS Widgets OpenGLWidgets)
# HttpServer 独立包名式调用（Qt6 组件式重复调用在本机未置 _FOUND，原因待查）
# HttpServer 独立包名式调用（组件式重复调用不置 _FOUND；依赖 WebSockets 需一并安装）
find_package(Qt6HttpServer QUIET)

set(CMAKE_AUTOMOC ON)

set(APP_SRC
  main.cpp
  session.cpp
  MainWindow.cpp
  WaterfallWidget.cpp
  meter_widgets.cpp
  dial_widget.cpp
  connect_dialog.cpp
  sstv_dialog.cpp
  dtmf_dialog.cpp
  voice_memory.cpp
  cw_keyer.cpp
  set_mode_dialog.cpp
  memory_channel.cpp
  aprs_panel.cpp)

if(Qt6HttpServer_FOUND)
  list(APPEND APP_SRC ../bus/bus_server.cpp ../bus/rigctld_server.cpp)
endif()

add_executable(radio-suite ${APP_SRC})

target_link_libraries(radio-suite PRIVATE
  Qt6::Widgets
  Qt6::OpenGLWidgets
  radio::core
  radio::dsp
  radio::audio
  radio::ui_common)
if(Qt6HttpServer_FOUND)
  target_link_libraries(radio-suite PRIVATE Qt6::HttpServer)
  target_compile_definitions(radio-suite PRIVATE RADIO_SUITE_HAVE_BUS=1)
  message(STATUS "radio-suite: HTTP 总线启用 (Qt6::HttpServer)")
endif()
if(TARGET radio::logbook)
  target_link_libraries(radio-suite PRIVATE radio::logbook)
  target_compile_definitions(radio-suite PRIVATE RADIO_SUITE_HAVE_LOGBOOK=1)
  find_package(Qt6 REQUIRED COMPONENTS Widgets)
endif()

# Windows: 便于 cmd/PowerShell 观察日志，M0 先保留控制台子系统；打包期(M5)再切 WIN32
