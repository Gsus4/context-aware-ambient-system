#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

export PORT="${PORT:-3000}"
export SMART_SPACE_HOST="${SMART_SPACE_HOST:-0.0.0.0}"
export DASHBOARD_DIR="${DASHBOARD_DIR:-src/smart_space/web/dashboard}"

check_port_available() {
  local pids=""
  if command -v lsof >/dev/null 2>&1; then
    pids="$(lsof -tiTCP:"${PORT}" -sTCP:LISTEN 2>/dev/null | tr '\n' ' ' | sed 's/[[:space:]]*$//')"
  elif command -v ss >/dev/null 2>&1; then
    pids="$(ss -ltnp "sport = :${PORT}" 2>/dev/null | sed -n 's/.*pid=\([0-9][0-9]*\).*/\1/p' | sort -u | tr '\n' ' ' | sed 's/[[:space:]]*$//')"
  fi

  if [ -n "$pids" ]; then
    echo "[ERROR] Port ${PORT} is already in use by PID(s): ${pids}"
    echo "[INFO] Not killing existing process. Stop it manually or set PORT to another value."
    exit 1
  fi
}

if ! command -v uv >/dev/null 2>&1; then
  echo "[ERROR] uv was not found."
  exit 1
fi

check_port_available

uv sync
exec uv run python -m smart_space.runtime
