@echo off
setlocal EnableExtensions

REM ============================================================
REM Dashboard - Multi Broker Hardware MQTT Mode
REM Light MQTT Broker    : 127.0.0.1:1883
REM Wearable MQTT Broker : 127.0.0.1:1883
REM Env MQTT Broker      : 127.0.0.1:1883
REM ============================================================

cd /d "%~dp0"

if not exist "package.json" (
  echo [ERROR] package.json not found.
  echo Please extract the ZIP first, then run this file inside the dashboard folder.
  pause
  exit /b 1
)

REM ===== Light / SmartLight MQTT =====
set "MQTT_HOST=127.0.0.1"
set "MQTT_PORT=1883"
set "SMARTLIGHT_NODE_ID=bedroom01"
set "SMARTLIGHT_TOPIC_ROOT=smartlight"

REM ===== Smart Platform v2 common root =====
set "INTEGRATION_TOPIC_ROOT=integration/smart/v1"

REM ===== Wearable / Heart Rate / SpO2 MQTT =====
set "WEARABLE_MQTT_ENABLED=true"
set "WEARABLE_MQTT_HOST=127.0.0.1"
set "WEARABLE_MQTT_PORT=1883"
set "WEARABLE_NODE_ID=pico_wearable01"

REM ===== Env / Temperature / Humidity / AQI / PIR / Fan MQTT =====
set "ENV_MQTT_ENABLED=true"
set "ENV_MQTT_HOST=127.0.0.1"
set "ENV_MQTT_PORT=1883"
set "ENV_SENSOR_NODE_ID=rpi_env01"
set "FAN_NODE_ID=rpi_env02"

REM ===== Optional Smart Platform v2 light node =====
set "LIGHT_INTEGRATION_NODE_ID=rpi_light01"

REM If your MQTT broker needs username/password, remove REM from the next lines.
REM set "MQTT_USERNAME=your_username"
REM set "MQTT_PASSWORD=your_password"
REM set "WEARABLE_MQTT_USERNAME=your_username"
REM set "WEARABLE_MQTT_PASSWORD=your_password"
REM set "ENV_MQTT_USERNAME=your_username"
REM set "ENV_MQTT_PASSWORD=your_password"

echo ============================================================
echo Dashboard - Multi Broker Hardware MQTT Mode
echo ============================================================
echo Folder        : %CD%
echo Dashboard     : http://localhost:3000
echo Light Broker  : %MQTT_HOST%:%MQTT_PORT%
echo Light Node    : %SMARTLIGHT_NODE_ID%
echo Wearable Broker: %WEARABLE_MQTT_HOST%:%WEARABLE_MQTT_PORT%
echo Wearable Node : %WEARABLE_NODE_ID%
echo Env Broker    : %ENV_MQTT_HOST%:%ENV_MQTT_PORT%
echo Env Node      : %ENV_SENSOR_NODE_ID%
echo Fan Node      : %FAN_NODE_ID%
echo ============================================================
echo.

where node.exe >nul 2>nul
if errorlevel 1 (
  echo [ERROR] node.exe was not found.
  echo Please install Node.js LTS, then open a new Command Prompt and run again.
  pause
  exit /b 1
)

where npm.cmd >nul 2>nul
if errorlevel 1 (
  echo [ERROR] npm.cmd was not found.
  echo Please install Node.js LTS, then open a new Command Prompt and run again.
  pause
  exit /b 1
)

if not exist "node_modules" (
  echo [1/2] Installing required packages...
  call npm.cmd install
  if errorlevel 1 (
    echo [ERROR] npm install failed.
    pause
    exit /b 1
  )
) else (
  echo [1/2] Required packages already installed.
)

echo [2/2] Starting Dashboard MQTT Bridge...
echo Keep this window open while using the dashboard.
echo.
node server\dashboard-mqtt-bridge.js

if errorlevel 1 (
  echo.
  echo [ERROR] Dashboard MQTT Bridge stopped with an error.
  echo Please copy the full error message and send it back for checking.
  pause
  exit /b 1
)

pause
