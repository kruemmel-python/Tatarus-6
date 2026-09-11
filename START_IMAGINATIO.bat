@echo off
setlocal
cd /d "%~dp0"
set "CMAKE_EXE=C:\Program Files\CMake\bin\cmake.exe"
if not exist "%CMAKE_EXE%" set "CMAKE_EXE=cmake.exe"

"%CMAKE_EXE%" -S . -B build-imaginatio -G "Visual Studio 17 2022" -A x64 -DTATARUS_BUILD_TESTS=OFF -DTATARUS_BUILD_EXAMPLES=ON -DTATARUS_BUILD_SHARED=OFF
if errorlevel 1 exit /b 1

"%CMAKE_EXE%" --build build-imaginatio --config Release --target tatarus_imaginatio_demo --parallel 2
if errorlevel 1 exit /b 1

"%~dp0build-imaginatio\Release\tatarus_imaginatio_demo.exe" "%~dp0imaginatio_output"
if errorlevel 1 exit /b 1

echo.
echo Der vollstaendige Lauf liegt in: %~dp0imaginatio_output
