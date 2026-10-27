# SPDX-License-Identifier: AGPL-3.0-or-later
# libradio_ui_common：app 与 logbook 共用的设置对话框（字体/代理）
# 放在两层之下，避免 logbook 反向依赖 app 层头文件。
find_package(Qt6 QUIET COMPONENTS Widgets Network)
if(Qt6Widgets_FOUND AND Qt6Network_FOUND)
  add_library(radio_ui_common STATIC font_proxy_settings.cpp)
  add_library(radio::ui_common ALIAS radio_ui_common)
  set_target_properties(radio_ui_common PROPERTIES AUTOMOC ON)
  target_compile_features(radio_ui_common PUBLIC cxx_std_20)
  target_include_directories(radio_ui_common PUBLIC "${CMAKE_SOURCE_DIR}/apps/desktop")
  target_link_libraries(radio_ui_common PUBLIC Qt6::Widgets Qt6::Network)
endif()
