@echo off
echo ===================================================
echo   Installing Wacom CT-0405-U Driver to Startup
echo ===================================================

set "EXE_PATH=%~dp0..\build\bin\Release\CT0405_ControlPanel.exe"
if not exist "%EXE_PATH%" (
    set "EXE_PATH=%~dp0..\build\bin\CT0405_ControlPanel.exe"
)

if not exist "%EXE_PATH%" (
    echo [ERROR] Could not find CT0405_ControlPanel.exe. Please build the project first.
    pause
    exit /b 1
)

reg add "HKCU\Software\Microsoft\Windows\CurrentVersion\Run" /v "WacomCT0405Driver" /t REG_SZ /d "\"%EXE_PATH%\" --minimized" /f
if %errorlevel% equ 0 (
    echo [SUCCESS] Driver registered to auto-start with Windows in system tray!
) else (
    echo [ERROR] Failed to write registry key.
)

pause
