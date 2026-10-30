@echo off
rem Android 交叉编译配置（arm64-v8a）：只建 Qt Core/Network 层（core/ft8/aprs/wsjtx/dsp），
rem 不建 GUI 与 Catch2 测试。依赖 D:\Qt\6.7.3 下的两份安装：
rem   android_arm64_v8a = 目标库；msvc2019_64 = host tools（moc/rcc/androiddeployqt）
setlocal
set ANDROID_SDK_ROOT=D:\Android\sdk
set ANDROID_NDK_ROOT=D:\Android\sdk\ndk\26.1.10909125
set JAVA_HOME=C:\Program Files\Eclipse Adoptium\jdk-21.0.12.101-hotspot
set ANDROID_QT=D:\Qt\6.7.3\android_arm64_v8a
set QT_HOST=D:\Qt\6.7.3\msvc2019_64
set NINJA=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe
set CMAKE_BIN=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin
set PATH=%CMAKE_BIN%;%QT_HOST%\bin;%JAVA_HOME%\bin;%PATH%

cmake -S "%~dp0.." -B "%~dp0..\build-android" -G Ninja ^
  -DCMAKE_MAKE_PROGRAM="%NINJA%" ^
  -DCMAKE_TOOLCHAIN_FILE=%ANDROID_QT%\lib\cmake\Qt6\qt.toolchain.cmake ^
  -DQT_HOST_PATH=%QT_HOST% ^
  -DANDROID_ABI=arm64-v8a ^
  -DANDROID_PLATFORM=android-23 ^
  -DCMAKE_BUILD_TYPE=Release ^
  -DBUILD_GUI=OFF ^
  -DRADIO_SUITE_BUILD_TESTS=OFF %*
endlocal
