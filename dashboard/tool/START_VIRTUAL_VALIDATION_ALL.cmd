@echo off
setlocal EnableExtensions

cd /d "%~dp0"

echo ============================================================
echo SmartLight Dashboard Virtual Validation
echo This starts Mock SmartLight and Dashboard in two windows.
echo Please make sure Mosquitto broker is running on 127.0.0.1:1883.
echo ============================================================
echo.

start "Mock SmartLight" cmd /k "START_MOCK_SMARTLIGHT_LOCAL.cmd"
timeout /t 2 /nobreak >nul
start "Dashboard Virtual" cmd /k "START_DASHBOARD_VIRTUAL_LOCAL.cmd"
