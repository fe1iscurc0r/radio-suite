@echo off
rem 首次运行会先跑 android_configure.bat；之后只在 CMakeLists 变化时由 ninja 自动重配
setlocal
set CMAKE_BIN=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin
set JAVA_HOME=C:\Program Files\Eclipse Adoptium\jdk-21.0.12.101-hotspot
set PATH=%CMAKE_BIN%;D:\Qt\6.7.3\msvc2019_64\bin;%JAVA_HOME%\bin;%PATH%
if not exist "%~dp0..\build-android\CMakeCache.txt" (
  call "%~dp0android_configure.bat" || exit /b 1
)
cmake --build "%~dp0..\build-android" --target %*
endlocal
