@echo off
setlocal enabledelayedexpansion

echo =======================================================
echo   Building Wacom CT-0405-U Driver & Control Panel
echo =======================================================

if not exist build (
    mkdir build
)

cd build

echo [*] Configuring CMake project...
cmake -G "Visual Studio 17 2022" -A x64 ..
if %errorlevel% neq 0 (
    echo [ERROR] CMake configuration failed.
    cd ..
    pause
    exit /b %errorlevel%
)

echo [*] Compiling Release binaries...
cmake --build . --config Release
if %errorlevel% neq 0 (
    echo [ERROR] Compilation failed.
    cd ..
    pause
    exit /b %errorlevel%
)

cd ..

echo =======================================================
echo   BUILD SUCCESSFUL!
echo   Binaries located in: build\bin\Release\
echo     - CT0405_ControlPanel.exe (GUI Application)
echo     - CT0405_CLI.exe          (Diagnostic CLI)
echo =======================================================

pause
