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
