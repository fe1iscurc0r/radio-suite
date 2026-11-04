# SPDX-License-Identifier: AGPL-3.0-or-later
# radio_bus：本机集成总线——HTTP bus_server（Qt6::HttpServer）+ rigctld 服务器
# （QTcpServer）。命令出口就是 RadioSession，故依赖 radio_session（PUBLIC）。
# bus_server.h 还暴露 dsp/fft.h、dsp/synth.h（合成谱源），radio::dsp 也须 PUBLIC。
#
# Qt6::HttpServer 不是所有套件都有（Qt 6.7.3 的安卓套件里没有，6.8.3 的安卓套件里有——
# 本仓已实测 bus_server.cpp 能为 arm64-v8a 编过并链进 libradio-suite_arm64-v8a.so），
# 所以这里静默查找、拿不到就整个目录跳过。别写 REQUIRED：那会让缺模块的套件
# （安卓交叉编译、发行版旧 Qt）全新配置直接失败。
# 下游只认 radio::bus 这个目标在不在（app/CMakeLists.txt 与 tests/CMakeLists.txt 都是
# 这么判的），不要再各自 find_package 一遍判 _FOUND——两个口径一分叉就会像
# CI run 111 那样「bus 跳过了、bus 测试还在链它」。
#
# 版本门槛 6.8 不是保险丝，是实测：Ubuntu 24.04 的 apt Qt 是 6.4.2，它的
# HttpServer API 和 6.8 是三种形状（req.headers() 返回 QList<QPair> 而非
# QHttpHeaders、QAbstractHttpServer::bind 返回 void、route 视图的 Responder
# 只收 &&，而 6.8 反过来只收 &——两边互斥）。CI 的 ubuntu 作业因此在 Build
# 阶段红了 5 次。与其为发行版旧 Qt 铺 #if 梯，不如把 bus 的真实依赖写进
# CMake：低于 6.8 就整个跳过，configure 明说，别默默编坏。
find_package(Qt6 REQUIRED COMPONENTS Network)
find_package(Qt6HttpServer 6.8 QUIET)
if(NOT TARGET Qt6::HttpServer)
  message(STATUS "radio_bus: 没有 Qt6::HttpServer 或版本低于 6.8，跳过 bus"
                 "（rigctld/HTTP 总线/SSE 不编；CI ubuntu 的 apt Qt 6.4.2 即此情形）")
  return()
endif()
# 再过一遍 Qt6 的组件通道：直接 find_package(Qt6HttpServer) 建出来的 Qt6::HttpServer
# 不在 Qt6 的已声明组件清单里，qt6_finalize_target 扫依赖时会警告
# 「The HttpServer target is mentioned as a dependency for radio::bus, but not
# declared. The linking might be incomplete」（安卓侧还关系到 androiddeployqt
# 会不会把 libQt6HttpServer.so 打进 APK）。这一句让它认账；QUIET + 目标判据，
# 不依赖 Qt6HttpServer_FOUND 那个变量。
find_package(Qt6 QUIET COMPONENTS HttpServer)

add_library(radio_bus STATIC bus_server.cpp rigctld_server.cpp)

add_library(radio::bus ALIAS radio_bus)

target_compile_features(radio_bus PUBLIC cxx_std_20)
target_include_directories(radio_bus PUBLIC
  "${CMAKE_SOURCE_DIR}/apps/desktop")
target_link_libraries(radio_bus PUBLIC
  radio_session
  radio::dsp
  Qt6::Network
  Qt6::HttpServer)
# bus 已无条件可用，宏仍 PUBLIC 保留：MainWindow 自检报告用它标记 rig_http_bus_built
target_compile_definitions(radio_bus PUBLIC RADIO_SUITE_HAVE_BUS=1)
set_target_properties(radio_bus PROPERTIES AUTOMOC ON)
