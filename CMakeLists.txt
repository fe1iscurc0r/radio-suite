# SPDX-License-Identifier: AGPL-3.0-or-later
# radio-suite — 顶层构建脚本
# 模块化 target：core / dsp / app 各自独立，任一模块失败不拖垮其他 target（工单硬约束 1）
cmake_minimum_required(VERSION 3.21)

project(radio-suite
  VERSION 0.1.0
  DESCRIPTION "Amateur radio control suite: CI-V control, spectrum waterfall, logbook (AGPL-3.0)"
  LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(BUILD_GUI "构建 Qt6 Widgets 应用；OFF 仍构建需 Qt6 Core/Network 的 core 与测试" ON)
option(RADIO_SUITE_BUILD_TESTS "构建 Catch2 单元测试" ON)
option(BUILD_ENGINE_SPIKE "构建 WO-274 rigplane 引擎 spike（需 Qt6 WebSockets）" OFF)

if(MSVC)
  # /utf-8: 源码含中文注释，防止 C4819 与乱码
  add_compile_options(/utf-8 /Zc:__cplusplus /permissive- /W4)
else()
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

enable_testing()

include(cmake/dependencies.cmake)

add_subdirectory(apps/desktop/core)
add_subdirectory(libs/dsp/dsp)  # WO-273 单仓裁决：dsp 双端共用（web 未来也用）
add_subdirectory(apps/desktop/logbook)  # 无 Qt6::Sql 环境自动跳过（内部判断）
add_subdirectory(apps/desktop/audio)    # RtAudio (MIT, vendor/)：SSTV/DV 播放
if(BUILD_GUI)
  add_subdirectory(apps/desktop/app)
endif()
if(BUILD_ENGINE_SPIKE)
  add_subdirectory(apps/spike)
endif()
if(RADIO_SUITE_BUILD_TESTS)
  add_subdirectory(tests)
endif()
