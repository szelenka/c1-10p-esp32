#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
PIO="$ROOT_DIR/scripts/pio_local.sh"
PY="$ROOT_DIR/.venv/bin/python"

ENV_NAME="esp32dev"
PORT=""
HOST=""
SERIAL_SECONDS=30
ENABLE_HTTP_CHECK=0

usage() {
  cat <<USAGE
Usage: $0 [--env esp32dev] [--port /dev/...] [--host <ip>] [--seconds 30]

Automates on-device validation with CHOPPER_VALIDATION_RUNTIME firmware.
- Always validates serial markers (NVS self-test + telemetry)
- Optionally validates HTTP endpoints if --host is provided
USAGE
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --env) ENV_NAME="$2"; shift 2 ;;
    --port) PORT="$2"; shift 2 ;;
    --host) HOST="$2"; ENABLE_HTTP_CHECK=1; shift 2 ;;
    --seconds) SERIAL_SECONDS="$2"; shift 2 ;;
    -h|--help) usage; exit 0 ;;
    *) echo "Unknown arg: $1" >&2; usage; exit 1 ;;
  esac
done

if [[ ! -x "$PIO" ]]; then
  echo "Missing PlatformIO wrapper: $PIO" >&2
  exit 1
fi

if [[ -z "$PORT" ]]; then
  echo "Auto-detecting serial port..."
  PORT="$($PIO device list --json-output 2>/dev/null | sed -n '/^\[/,$p' | \
    $PY -c 'import json,sys
items=json.load(sys.stdin)
cands=[it.get("port","") for it in items if "bluetooth" not in it.get("port","").lower() and "debug-console" not in it.get("port","").lower()]
pref=[p for p in cands if any(x in p for x in ["usb","ttyUSB","ttyACM","SLAB","wch","cu.usb","cu.SLAB"])]
chosen=(pref or cands or [""])[0]
if chosen:
    print(chosen)
    raise SystemExit(0)
raise SystemExit(1)') " || true
fi

PORT="$(echo "$PORT" | tr -d '[:space:]')"

if [[ -z "$PORT" ]]; then
  echo "No suitable serial port detected. Pass --port explicitly." >&2
  exit 1
fi

echo "Using env=$ENV_NAME port=$PORT"

echo "[1/5] Build validation firmware"
$PIO run -e "$ENV_NAME"

echo "[2/5] Upload validation firmware"
$PIO run -e "$ENV_NAME" -t upload --upload-port "$PORT"

echo "[3/5] Validate serial markers"
$PY "$ROOT_DIR/scripts/serial_validation_check.py" --port "$PORT" --seconds "$SERIAL_SECONDS"

echo "[4/5] Optional HTTP checks"
if [[ "$ENABLE_HTTP_CHECK" == "1" ]]; then
  $PY "$ROOT_DIR/scripts/telemetry_http_check.py" --host "$HOST"
else
  echo "Skipping HTTP checks (no --host provided)"
fi

echo "[5/5] Done"
echo "On-device validation finished successfully."
