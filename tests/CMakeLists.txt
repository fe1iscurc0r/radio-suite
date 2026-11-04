# SPDX-License-Identifier: AGPL-3.0-or-later
# Catch2 v3 测试目标：协议对照 / 白名单 / 安全门禁 / mock 闭环 / DSP / RS-BA1 UDP
find_package(Qt6 REQUIRED COMPONENTS Core)

add_executable(radio_tests
  test_civ.cpp
  test_bands.cpp
  test_safety.cpp
  test_transport_mock.cpp
  test_dsp.cpp
  test_rsba1_proto.cpp
  test_sstv.cpp
  test_dtmf.cpp
  test_audio_stream.cpp
  test_audio_bridge.cpp
  test_aprs.cpp
  test_wsjtx.cpp
  test_ft8.cpp
  test_ft8_slot.cpp
  test_ft8_tx.cpp
  test_engine_spike.cpp
  "${CMAKE_SOURCE_DIR}/apps/spike/scope_wire.cpp")

target_link_libraries(radio_tests PRIVATE
  radio::core
  radio::aprs
  radio::wsjtx
  radio::ft8_codec
  radio::dsp
  Qt6::Core
  Catch2::Catch2WithMain)

target_include_directories(radio_tests PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}"
  "${CMAKE_SOURCE_DIR}"
  "${CMAKE_SOURCE_DIR}/apps/desktop")
target_compile_definitions(radio_tests PRIVATE
  RADIO_SUITE_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")

include(Catch)
catch_discover_tests(radio_tests)

# RS-BA1 本机环回（假电台服务端 + 真 UDP 传输）：需要 radio_core 编入
# transport_udp.cpp，即 RADIO_SUITE_UDP=ON 且找到 Qt6::Network。
find_package(Qt6 QUIET COMPONENTS Network)
if(RADIO_SUITE_UDP AND Qt6Network_FOUND)
  target_sources(radio_tests PRIVATE test_rsba1_loopback.cpp fake_rsba1_server.cpp
                                     test_transport_tcp.cpp)
  target_link_libraries(radio_tests PRIVATE Qt6::Network)
endif()

add_executable(radio_session_tests
  test_session_tx.cpp
  test_tcp_gateway.cpp)
target_include_directories(radio_session_tests PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}"
  "${CMAKE_SOURCE_DIR}/apps/desktop")
target_link_libraries(radio_session_tests PRIVATE
  radio::session
  Qt6::Core
  Catch2::Catch2WithMain)
set_target_properties(radio_session_tests PROPERTIES AUTOMOC ON)
catch_discover_tests(radio_session_tests)

# 「要不要编 bus 测试」的口径只能有一个：radio::bus 这个目标在不在。
# bus/CMakeLists.txt 已经按 Qt6::HttpServer 的存在与版本（>=6.8）决定建不建这个库；
# 这里原来自己 find_package(Qt6HttpServer QUIET) + if(_FOUND) 判一遍，两个口径会分叉——
# Ubuntu 24.04 的 apt Qt 是 6.4.2，_FOUND 为真但 bus 已跳过，target_link_libraries(radio::bus)
# 当场报错（CI run 111 就是这么红的）。
if(TARGET radio::bus)
  find_package(Qt6 REQUIRED COMPONENTS Network)
  # imported target 只在建它的那个目录作用域里可见，所以本目录仍需 find_package 一次；
  # 版本号与 bus 里写死成同一个 6.8，两边一旦漂移就 configure 直接报错，
  # 不再出现「Qt6::HttpServer target was not found」这种看不出根因的错。
  find_package(Qt6HttpServer 6.8 REQUIRED)
  add_executable(radio_bus_tests
    test_bus.cpp
    test_rigctld.cpp)
  target_include_directories(radio_bus_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_SOURCE_DIR}/apps/desktop")
  target_link_libraries(radio_bus_tests PRIVATE
    radio::bus
    Qt6::Network
    Qt6::HttpServer
    Catch2::Catch2WithMain)
  set_target_properties(radio_bus_tests PROPERTIES AUTOMOC ON)
  catch_discover_tests(radio_bus_tests)
endif()

if(BUILD_GUI AND TARGET radio::aprs_db)
  find_package(Qt6 REQUIRED COMPONENTS Widgets Network Sql)
  find_package(Qt6 QUIET COMPONENTS SerialPort)
  add_executable(radio_aprs_ui_tests
    test_aprs_ui.cpp
    "${CMAKE_SOURCE_DIR}/apps/desktop/app/aprs_panel.cpp"
    "${CMAKE_SOURCE_DIR}/apps/desktop/app/aprs_map.cpp"
    "${CMAKE_SOURCE_DIR}/apps/desktop/app/aprs_messages_dialog.cpp"
    "${CMAKE_SOURCE_DIR}/apps/desktop/app/aprs_rf.cpp"
    "${CMAKE_SOURCE_DIR}/apps/desktop/app/touch_dialog.cpp")
  target_link_libraries(radio_aprs_ui_tests PRIVATE
    radio::aprs_db
    Qt6::Widgets
    Qt6::Network
    Qt6::Sql
    Catch2::Catch2WithMain)
  if(Qt6SerialPort_FOUND)
    target_link_libraries(radio_aprs_ui_tests PRIVATE Qt6::SerialPort)
    target_compile_definitions(radio_aprs_ui_tests
      PRIVATE RADIO_SUITE_HAVE_SERIALPORT=1)
  endif()
  target_include_directories(radio_aprs_ui_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_SOURCE_DIR}/apps/desktop")
  set_target_properties(radio_aprs_ui_tests PROPERTIES AUTOMOC ON)
  catch_discover_tests(radio_aprs_ui_tests)
endif()

if(BUILD_GUI)
  find_package(Qt6 REQUIRED COMPONENTS Widgets Network)
  add_executable(radio_wsjtx_ui_tests
    test_wsjtx_ui.cpp
    test_multicast_lock.cpp
    "${CMAKE_SOURCE_DIR}/apps/desktop/app/wsjtx_client.cpp"
    "${CMAKE_SOURCE_DIR}/apps/desktop/app/multicast_lock.cpp"
    "${CMAKE_SOURCE_DIR}/apps/desktop/app/wsjtx_panel.cpp"
    "${CMAKE_SOURCE_DIR}/apps/desktop/app/touch_dialog.cpp")
  target_link_libraries(radio_wsjtx_ui_tests PRIVATE
    radio::wsjtx
    Qt6::Widgets
    Qt6::Network
    Catch2::Catch2WithMain)
  target_include_directories(radio_wsjtx_ui_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_SOURCE_DIR}/apps/desktop")
  set_target_properties(radio_wsjtx_ui_tests PROPERTIES AUTOMOC ON)
  catch_discover_tests(radio_wsjtx_ui_tests)

  # FT8 本机解码面板：PCM→槽解码→表格，双击回填（离屏）
  add_executable(radio_ft8_ui_tests
    test_ft8_ui.cpp
    "${CMAKE_SOURCE_DIR}/apps/desktop/app/ft8_panel.cpp")
  target_link_libraries(radio_ft8_ui_tests PRIVATE
    radio::ft8_codec
    Qt6::Widgets
    Catch2::Catch2WithMain)
  target_include_directories(radio_ft8_ui_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_SOURCE_DIR}/apps/desktop")
  set_target_properties(radio_ft8_ui_tests PROPERTIES AUTOMOC ON)
  catch_discover_tests(radio_ft8_ui_tests)

  # 本机声学环回：扬声器播一槽、麦克风收一槽。要真出声故默认跳过，不进 CI。
  if(TARGET radio::audio)
    add_executable(radio_ft8_acoustic_tests test_ft8_acoustic.cpp)
    target_link_libraries(radio_ft8_acoustic_tests PRIVATE
      radio::ft8_codec
      radio::audio
      Qt6::Core
      Catch2::Catch2WithMain)
    target_include_directories(radio_ft8_acoustic_tests PRIVATE
      "${CMAKE_CURRENT_SOURCE_DIR}"
      "${CMAKE_SOURCE_DIR}/apps/desktop")
    catch_discover_tests(radio_ft8_acoustic_tests)
  endif()
endif()

if(TARGET radio::aprs_db)
  find_package(Qt6 REQUIRED COMPONENTS Sql)
  add_executable(radio_aprs_db_tests test_aprs_persist.cpp)
  target_link_libraries(radio_aprs_db_tests PRIVATE
    radio::aprs_db
    Qt6::Sql
    Catch2::Catch2WithMain)
  target_include_directories(radio_aprs_db_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_SOURCE_DIR}/apps/desktop")
  catch_discover_tests(radio_aprs_db_tests)
endif()

if(TARGET radio::logbook)
  find_package(Qt6 REQUIRED COMPONENTS Sql)
  add_executable(radio_logbook_tests test_logbook.cpp)
  target_include_directories(radio_logbook_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_SOURCE_DIR}/apps/desktop")
  target_link_libraries(radio_logbook_tests PRIVATE
    radio::logbook
    Qt6::Sql
    Catch2::Catch2WithMain)
  catch_discover_tests(radio_logbook_tests)
endif()

# 对话框接线测试：需要 Widgets（BUILD_GUI=OFF 的 CI 任务自动跳过）
if(BUILD_GUI AND TARGET radio::ui_common)
  find_package(Qt6 REQUIRED COMPONENTS Widgets Network)
  add_executable(radio_ui_tests
    test_dialogs.cpp
    test_audio_ui.cpp
    test_touch_dialog.cpp
    test_launch_config.cpp
    test_device_report.cpp
    "${CMAKE_SOURCE_DIR}/apps/desktop/app/launch_config.cpp"
    "${CMAKE_SOURCE_DIR}/apps/desktop/app/connect_dialog.cpp"
    "${CMAKE_SOURCE_DIR}/apps/desktop/app/cw_keyer.cpp"
    "${CMAKE_SOURCE_DIR}/apps/desktop/app/set_mode_dialog.cpp"
    "${CMAKE_SOURCE_DIR}/apps/desktop/app/memory_channel.cpp"
    "${CMAKE_SOURCE_DIR}/apps/desktop/app/repeater_dialog.cpp"
    "${CMAKE_SOURCE_DIR}/apps/desktop/app/audio_dialog.cpp"
    "${CMAKE_SOURCE_DIR}/apps/desktop/app/device_report.cpp"
    "${CMAKE_SOURCE_DIR}/apps/desktop/app/self_check_dialog.cpp"
    "${CMAKE_SOURCE_DIR}/apps/desktop/app/touch_dialog.cpp"
    "${CMAKE_SOURCE_DIR}/apps/desktop/app/audio_stats.cpp")
  target_include_directories(radio_ui_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_SOURCE_DIR}/apps/desktop")
  target_link_libraries(radio_ui_tests PRIVATE
    radio::core
    radio::session
    Qt6::Widgets
    Qt6::Network
    Catch2::Catch2WithMain)
  set_target_properties(radio_ui_tests PROPERTIES AUTOMOC ON)
  catch_discover_tests(radio_ui_tests)
endif()
