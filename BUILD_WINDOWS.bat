@echo off
setlocal
set "CMAKE_EXE=C:\Program Files\CMake\bin\cmake.exe"
if not exist "%CMAKE_EXE%" set "CMAKE_EXE=cmake.exe"
"%CMAKE_EXE%" -S . -B build -G "Visual Studio 17 2022" -A x64
if errorlevel 1 exit /b 1
rem Bound MSVC concurrency because the Cortex translation units are memory-heavy.
"%CMAKE_EXE%" --build build --config Release --parallel 2
if errorlevel 1 exit /b 1
"%CMAKE_EXE%" --build build --config Release --target RUN_TESTS
