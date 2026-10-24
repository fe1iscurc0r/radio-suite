# SPDX-License-Identifier: AGPL-3.0-or-later
# 第三方依赖（FetchContent，首次 configure 需要网络；产物缓存在 build/_deps）
# 许可清单见 NOTICE.md，原文见 LICENSES/

include(FetchContent)

# ---- pocketfft（BSD-3，header-only C++ FFT）----
# 官方仓库在 MPCDF GitLab，此为其作者 GitHub 镜像 cpp 分支的固定提交。
# 若离线构建：设置 FETCHCONTENT_SOURCE_DIR_POCKETFFT=<本地目录>（内含 pocketfft_hdronly.h）
set(RADIO_SUITE_POCKETFFT_GIT "https://github.com/mreineck/pocketfft" CACHE STRING
    "pocketfft 仓库地址")
set(RADIO_SUITE_POCKETFFT_TAG "4d865cba24359b05b37da4e75aee24498c3b78e7" CACHE STRING
    "pocketfft 固定提交（勿用分支名，保证可复现）")
FetchContent_Declare(pocketfft
  GIT_REPOSITORY "${RADIO_SUITE_POCKETFFT_GIT}"
  GIT_TAG        "${RADIO_SUITE_POCKETFFT_TAG}")
FetchContent_MakeAvailable(pocketfft)
# pocketfft 无 CMakeLists.txt，MakeAvailable 只负责取源码；头文件路径：
#   ${pocketfft_SOURCE_DIR}  （由 radio_dsp 的 include 路径引用）

# ---- Catch2 v3（BSL-1.0，仅测试）----
if(RADIO_SUITE_BUILD_TESTS)
  set(RADIO_SUITE_CATCH2_TAG "v3.7.1" CACHE STRING "Catch2 固定 tag")
  FetchContent_Declare(Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        "${RADIO_SUITE_CATCH2_TAG}")
  FetchContent_MakeAvailable(Catch2)
  list(APPEND CMAKE_MODULE_PATH "${catch2_SOURCE_DIR}/extras")
endif()
