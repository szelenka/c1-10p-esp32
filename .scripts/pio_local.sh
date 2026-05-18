#!/usr/bin/env bash
set -euo pipefail
ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
export PLATFORMIO_CORE_DIR="$ROOT_DIR/.pio-home"
exec "$ROOT_DIR/.venv/bin/pio" "$@"
