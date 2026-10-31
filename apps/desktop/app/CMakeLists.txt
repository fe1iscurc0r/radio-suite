# SPDX-License-Identifier: AGPL-3.0-or-later
# radio-suite 可执行：Qt Widgets + QOpenGLWidget 瀑布 + HamLog 融合 + APRS 面板
# + --headless --bus HTTP 总线（M5 第一片，bus 源直接编入；全量时再独立成库）
find_package(Qt6 6.4 REQUIRED COMPONENTS Widgets OpenGLWidgets Network)
find_package(Qt6 QUIET COMPONENTS SerialPort)
# HttpServer 独立包名式调用（Qt6 组件式重复调用在本机未置 _FOUND，原因待查）
# HttpServer 独立包名式调用（组件式重复调用不置 _FOUND；依赖 WebSockets 需一并安装）
find_package(Qt6HttpServer QUIET)

set(CMAKE_AUTOMOC ON)

set(APP_SRC
  main.cpp
  launch_config.cpp
  session.cpp
  network_audio.cpp
  audio_stats.cpp
  audio_dialog.cpp
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
  repeater_dialog.cpp
  aprs_panel.cpp
  aprs_map.cpp
  aprs_messages_dialog.cpp
  aprs_rf.cpp
  wsjtx_client.cpp
  wsjtx_panel.cpp
  ft8_panel.cpp)

if(Qt6HttpServer_FOUND)
  list(APPEND APP_SRC ../bus/bus_server.cpp ../bus/rigctld_server.cpp)
endif()

# Android 用 qt_add_executable：它会挂上 androiddeployqt 的部署设置与 apk 目标
if(ANDROID)
  qt_add_executable(radio-suite ${APP_SRC})
else()
  add_executable(radio-suite ${APP_SRC})
endif()

target_link_libraries(radio-suite PRIVATE
  Qt6::Widgets
  Qt6::OpenGLWidgets
  Qt6::Network
  radio::core
  radio::dsp
  radio::audio
  radio::aprs
  radio::wsjtx
  radio::ft8_codec
  radio::ui_common)
if(TARGET radio::aprs_db)
  target_link_libraries(radio-suite PRIVATE radio::aprs_db)
endif()
if(Qt6SerialPort_FOUND)
  target_link_libraries(radio-suite PRIVATE Qt6::SerialPort)
  target_compile_definitions(radio-suite PRIVATE RADIO_SUITE_HAVE_SERIALPORT=1)
  message(STATUS "radio-suite: KISS 串口启用 (Qt6::SerialPort)")
endif()
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

# Android 打包：清单覆盖 Qt 模板默认值（包名/INTERNET/RECORD_AUDIO）
if(ANDROID)
  set_target_properties(radio-suite PROPERTIES
    QT_ANDROID_PACKAGE_SOURCE_DIR "${CMAKE_SOURCE_DIR}/packaging/android"
    QT_ANDROID_PACKAGE_NAME "org.radiosuite.app"
    QT_ANDROID_VERSION_NAME "0.1.0"
    QT_ANDROID_VERSION_CODE "1"
    QT_ANDROID_MIN_SDK_VERSION "26"
    QT_ANDROID_TARGET_SDK_VERSION "34")

  # Qt6::Sql 只挂在 radio_aprs_db / radio_logbook 两个**静态库**上，androiddeployqt
  # 的插件清单（deployment json 里的 android-deploy-plugins）按可执行目标的依赖链
  # 收集，静态库带的插件进不来：APK 里有 libQt6Sql.so 却没有 sqldrivers/qsqlite，
  # 手机上 QSQLITE 驱动不可用，APRS 持久化与 HamLog 整体失效（2026-10-31 真机 D3：
  # 状态栏「APRS 保存失败：Qt SQLite 驱动不可用」）。修法是把驱动插件直接挂到
  # 可执行目标上。
  # 别用 QT_ANDROID_EXTRA_PLUGINS：那个键在 androiddeployqt 里是「往 assets/ 拷外部
  # 资源」，喂 .so 路径或类型名都会 "External resource … does not exist or not a
  # correct directory!" 停建（两种都实测过），指向插件目录则只会把插件塞进 assets，
  # Qt 运行时不从那儿加载插件。
  find_package(Qt6 QUIET COMPONENTS Sql)
  if(TARGET Qt6::QSQLiteDriverPlugin)
    qt_import_plugins(radio-suite INCLUDE Qt6::QSQLiteDriverPlugin)
    get_target_property(_rs_qt_plugins radio-suite QT_PLUGINS)
    message(STATUS "radio-suite(Android): 可执行目标挂上的插件 = ${_rs_qt_plugins}")
  else()
    # Qt6_DIR = <Qt>/lib/cmake/Qt6，往上三层才是 Qt 根（少算一层指到 lib，glob 会落空）
    get_filename_component(RADIO_QT_ROOT "${Qt6_DIR}/../../.." ABSOLUTE)
    message(WARNING
      "radio-suite(Android): 看不到 Qt6::QSQLiteDriverPlugin 目标（Qt6_DIR=${Qt6_DIR}，"
      "实盘插件在 ${RADIO_QT_ROOT}/plugins/sqldrivers），APK 不会带 SQLite 驱动，"
      "手机上 APRS 持久化与 HamLog 仍不可用")
  endif()
endif()

# Windows 运行部署：刷新 packaging/out-win（exe + Qt DLL/插件），双击即可运行
if(WIN32)
  get_target_property(_radio_qmake Qt6::qmake IMPORTED_LOCATION)
  get_filename_component(_radio_qt_bin "${_radio_qmake}" DIRECTORY)
  find_program(RADIO_WINDEPLOYQT windeployqt HINTS "${_radio_qt_bin}")
  if(RADIO_WINDEPLOYQT)
    set(RADIO_DEPLOY_DIR "${CMAKE_SOURCE_DIR}/packaging/out-win")
    add_custom_target(deploy_win
      COMMAND "${CMAKE_COMMAND}" -E make_directory "${RADIO_DEPLOY_DIR}"
      COMMAND "${CMAKE_COMMAND}" -E copy_if_different
              "$<TARGET_FILE:radio-suite>" "${RADIO_DEPLOY_DIR}/"
      COMMAND "${RADIO_WINDEPLOYQT}" --no-translations
              --dir "${RADIO_DEPLOY_DIR}" "$<TARGET_FILE:radio-suite>"
      DEPENDS radio-suite
      COMMENT "windeployqt: 刷新 packaging/out-win（Qt DLL/插件 + exe）")
  else()
    message(STATUS "radio-suite: 未找到 windeployqt，跳过 deploy_win")
  endif()
endif()

# Windows: 便于 cmd/PowerShell 观察日志，M0 先保留控制台子系统；打包期(M5)再切 WIN32
