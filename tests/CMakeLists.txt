# SPDX-License-Identifier: AGPL-3.0-or-later
# Catch2 v3 测试目标：协议对照 / 白名单 / 安全门禁 / mock 闭环 / DSP / RS-BA1 UDP
add_executable(radio_tests
  test_civ.cpp
  test_bands.cpp
  test_safety.cpp
  test_transport_mock.cpp
  test_dsp.cpp
  test_rsba1_proto.cpp
  test_sstv.cpp)

target_link_libraries(radio_tests PRIVATE
  radio::core
  radio::dsp
  Catch2::Catch2WithMain)

target_include_directories(radio_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_definitions(radio_tests PRIVATE
  RADIO_SUITE_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")

include(Catch)
catch_discover_tests(radio_tests)
