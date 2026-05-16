#!/usr/bin/env bash

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR" || exit 1

export PORT="${PORT:-3000}"
export DASHBOARD_DIR="${DASHBOARD_DIR:-src/smart_space/web/dashboard}"

if ! command -v uv >/dev/null 2>&1; then
  echo "[ERROR] uv was not found."
  exit 1
fi

uv sync
uv run uvicorn smart_space.main:app --host 0.0.0.0 --port "$PORT"
