# SPDX-License-Identifier: AGPL-3.0-or-later
# libradio_logbook：HamLog Log.db 直读直写 + ADIF 导出（M4 提前：师傅点名融合）
find_package(Qt6 QUIET COMPONENTS Sql)
if(Qt6Sql_FOUND)
  find_package(Qt6 QUIET COMPONENTS Widgets)
  add_library(radio_logbook STATIC logbook.cpp hamlog_workbench.cpp qso_edit_dialog.cpp)
  add_library(radio::logbook ALIAS radio_logbook)
  set_target_properties(radio_logbook PROPERTIES AUTOMOC ON)  # HamLogWorkbench 带 Q_OBJECT
  target_compile_features(radio_logbook PUBLIC cxx_std_20)
  target_include_directories(radio_logbook PUBLIC "${CMAKE_SOURCE_DIR}/apps/desktop")
  target_link_libraries(radio_logbook PUBLIC Qt6::Sql Qt6::Widgets)
  message(STATUS "radio_logbook: HamLog 融合模块启用 (Qt6::Sql + Widgets)")
else()
  message(STATUS "radio_logbook: 未找到 Qt6::Sql，日志模块禁用")
endif()
