# SPDX-License-Identifier: AGPL-3.0-or-later
# radio-suite 可执行：Qt Widgets + QOpenGLWidget 瀑布
find_package(Qt6 6.4 REQUIRED COMPONENTS Widgets OpenGLWidgets)

set(CMAKE_AUTOMOC ON)

add_executable(radio-suite
  main.cpp
  session.cpp
  MainWindow.cpp
  WaterfallWidget.cpp)

target_link_libraries(radio-suite PRIVATE
  Qt6::Widgets
  Qt6::OpenGLWidgets
  radio::core
  radio::dsp)

# Windows: 便于 cmd/PowerShell 观察日志，M0 先保留控制台子系统；打包期(M5)再切 WIN32
