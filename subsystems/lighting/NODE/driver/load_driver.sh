#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
./smartlight_load "${1:-/dev/serial0}"
