# SPDX-License-Identifier: AGPL-3.0-or-later
# 第三方 vendored 源码：各自许可证保留在子目录（见 NOTICE.md）
enable_language(C)

add_library(radio_ft8lib STATIC
  ft8_lib/ft8/constants.c
  ft8_lib/ft8/crc.c
  ft8_lib/ft8/decode.c
  ft8_lib/ft8/encode.c
  ft8_lib/ft8/ldpc.c
  ft8_lib/ft8/message.c
  ft8_lib/ft8/text.c
  ft8_lib/common/monitor.c
  ft8_lib/fft/kiss_fft.c
  ft8_lib/fft/kiss_fftr.c
  ft8_lib/common/compat_msvc.c)
add_library(radio::ft8lib ALIAS radio_ft8lib)

target_include_directories(radio_ft8lib PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/ft8_lib")
set_target_properties(radio_ft8lib PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON)

# 上游代码不遵守本仓 /W4，第三方告警不参与门禁
target_compile_definitions(radio_ft8lib PRIVATE _USE_MATH_DEFINES _CRT_SECURE_NO_WARNINGS)
if(MSVC)
  target_compile_options(radio_ft8lib PRIVATE /W0)
else()
  target_compile_options(radio_ft8lib PRIVATE -w)
endif()

# dnwrnr/sgp4 的 libsgp4（Apache-2.0，见 third_party/sgp4/）：TLE 解析 + SGP4/SDP4 传播
# + 大地/天顶坐标。纯标准 C++17，不碰 Qt，所以安卓交叉编译与三平台 CI 都直接吃得到。
add_library(radio_sgp4 STATIC
  sgp4/libsgp4/CoordGeodetic.cpp
  sgp4/libsgp4/CoordTopocentric.cpp
  sgp4/libsgp4/CsvTleLoader.cpp
  sgp4/libsgp4/DateTime.cpp
  sgp4/libsgp4/DecayedException.cpp
  sgp4/libsgp4/Eci.cpp
  sgp4/libsgp4/Globals.cpp
  sgp4/libsgp4/Observer.cpp
  sgp4/libsgp4/OrbitalElements.cpp
  sgp4/libsgp4/SGP4.cpp
  sgp4/libsgp4/SatelliteException.cpp
  sgp4/libsgp4/SolarPosition.cpp
  sgp4/libsgp4/TimeSpan.cpp
  sgp4/libsgp4/Tle.cpp
  sgp4/libsgp4/TleException.cpp
  sgp4/libsgp4/Util.cpp
  sgp4/libsgp4/Vector.cpp)
add_library(radio::sgp4 ALIAS radio_sgp4)

# 上游源码内部是 #include <libsgp4/X.h>，所以暴露的是父目录而不是 libsgp4 本身
target_include_directories(radio_sgp4 PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/sgp4")
set_target_properties(radio_sgp4 PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)

target_compile_definitions(radio_sgp4 PRIVATE _USE_MATH_DEFINES _CRT_SECURE_NO_WARNINGS)
if(MSVC)
  target_compile_options(radio_sgp4 PRIVATE /W0)
else()
  target_compile_options(radio_sgp4 PRIVATE -w)
endif()
