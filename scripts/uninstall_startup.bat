@echo off
echo ===================================================
echo   Removing Wacom CT-0405-U Driver from Startup
echo ===================================================

reg delete "HKCU\Software\Microsoft\Windows\CurrentVersion\Run" /v "WacomCT0405Driver" /f
if %errorlevel% equ 0 (
    echo [SUCCESS] Removed Wacom CT-0405-U Driver from Windows Startup.
) else (
    echo [NOTE] Driver was not registered in Startup or already removed.
)

pause
