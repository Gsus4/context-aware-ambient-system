@echo off
setlocal EnableExtensions

REM ============================================================
REM Virtual SmartLight Device - Local MQTT
REM This simulates Gateway + MCU for Dashboard validation.
REM ============================================================

cd /d "%~dp0"

set "MQTT_HOST=127.0.0.1"
set "MQTT_PORT=1883"
set "SMARTLIGHT_NODE_ID=bedroom01"
set "SMARTLIGHT_TOPIC_ROOT=smartlight"

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
  echo [Mock] node_modules not found. Running npm install...
  npm.cmd install
  if errorlevel 1 (
    echo [ERROR] npm install failed.
    pause
    exit /b 1
  )
)

echo ============================================================
echo Virtual SmartLight Device
echo Broker : %MQTT_HOST%:%MQTT_PORT%
echo Topic  : %SMARTLIGHT_TOPIC_ROOT%/%SMARTLIGHT_NODE_ID%/#
echo ============================================================
echo.

node mock_smartlight_device.js
pause
