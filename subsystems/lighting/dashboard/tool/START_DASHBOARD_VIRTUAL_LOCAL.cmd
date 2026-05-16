@echo off
setlocal EnableExtensions

REM ============================================================
REM Dashboard - Virtual Local MQTT Validation Mode
REM Broker: 127.0.0.1:1883
REM Use this when there is no physical Pico W / Gateway device.
REM ============================================================

cd /d "%~dp0"

set "MQTT_HOST=127.0.0.1"
set "MQTT_PORT=1883"
set "SMARTLIGHT_NODE_ID=bedroom01"
set "SMARTLIGHT_TOPIC_ROOT=smartlight"

REM Disable other modules during LED-only virtual validation.
set "WEARABLE_MQTT_ENABLED=false"
set "ENV_MQTT_ENABLED=false"

REM Avoid background automatic LED command interfering with manual custom tests.
set "AUTO_ACTIVITY_LED_ENABLED=false"
set "AUTO_HR_LED_ENABLED=false"

if not exist "package.json" (
  echo [ERROR] package.json not found. Please run this file inside dashboard folder.
  pause
  exit /b 1
)

where node.exe >nul 2>nul
if errorlevel 1 (
  echo [ERROR] node.exe was not found. Please install Node.js LTS.
  pause
  exit /b 1
)

where npm.cmd >nul 2>nul
if errorlevel 1 (
  echo [ERROR] npm.cmd was not found. Please install Node.js LTS.
  pause
  exit /b 1
)

if not exist "node_modules" (
  echo [Dashboard] node_modules not found. Running npm install...
  npm.cmd install
  if errorlevel 1 (
    echo [ERROR] npm install failed.
    pause
    exit /b 1
  )
)

echo ============================================================
echo Dashboard Virtual Mode
echo Dashboard : http://localhost:3000
echo Broker    : %MQTT_HOST%:%MQTT_PORT%
echo Node ID   : %SMARTLIGHT_NODE_ID%
echo ============================================================
echo.

start "" "http://localhost:3000"
npm.cmd start
pause
