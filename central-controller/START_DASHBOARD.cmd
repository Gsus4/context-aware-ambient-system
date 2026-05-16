@echo off
setlocal EnableExtensions

cd /d "%~dp0"

if "%PORT%"=="" set "PORT=3000"
if "%DASHBOARD_DIR%"=="" set "DASHBOARD_DIR=src/smart_space/web/dashboard"

where uv.exe >nul 2>nul
if errorlevel 1 (
  echo [ERROR] uv.exe was not found.
  exit /b 1
)

uv sync
if errorlevel 1 exit /b 1

uv run uvicorn smart_space.main:app --host 0.0.0.0 --port %PORT%
