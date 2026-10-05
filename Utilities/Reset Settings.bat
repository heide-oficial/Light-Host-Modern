@echo off
setlocal

echo Reset settings for LightHostModern?
choice /C YN /M "Reset saved settings on the next launch"
if errorlevel 2 (
    echo Settings not altered.
    exit /b 0
)

powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Reset Settings.ps1"
if errorlevel 1 (
    echo Could not schedule the settings reset.
    exit /b 1
)
echo Settings reset scheduled. Close and reopen LightHostModern to apply it.
