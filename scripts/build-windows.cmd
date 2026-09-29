@echo off
rem Double-click to install prerequisites (if missing) and build both x64 and arm64.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0setup-windows.ps1" %*
echo.
pause
