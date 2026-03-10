#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
PIO="$ROOT_DIR/scripts/pio_local.sh"
PY="$ROOT_DIR/.venv/bin/python"

ENV_NAME="esp32dev-validation"
PORT=""
HOST=""
SERIAL_SECONDS=30
ENABLE_HTTP_CHECK=0
TELEMETRY_MODE="full"
REQUIRE_NVS_PASS=0
LIST_PORTS=0

usage() {
  cat <<USAGE
Usage: $0 [--env esp32dev-validation] [--port /dev/...] [--host <ip>] [--seconds 30]
          [--telemetry-mode full|compact|either] [--require-nvs-pass] [--list-ports]

Automates on-device validation with serial marker checks and optional HTTP checks.
- Serial telemetry mode defaults to full JSON (UI-friendly)
- NVS marker check is optional (enable with --require-nvs-pass)
- Optionally validates HTTP endpoints if --host is provided
USAGE
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --env) ENV_NAME="$2"; shift 2 ;;
    --port) PORT="$2"; shift 2 ;;
    --host) HOST="$2"; ENABLE_HTTP_CHECK=1; shift 2 ;;
    --seconds) SERIAL_SECONDS="$2"; shift 2 ;;
    --telemetry-mode) TELEMETRY_MODE="$2"; shift 2 ;;
    --require-nvs-pass) REQUIRE_NVS_PASS=1; shift 1 ;;
    --list-ports) LIST_PORTS=1; shift 1 ;;
    -h|--help) usage; exit 0 ;;
    *) echo "Unknown arg: $1" >&2; usage; exit 1 ;;
  esac
done

if [[ ! -x "$PIO" ]]; then
  echo "Missing PlatformIO wrapper: $PIO" >&2
  exit 1
fi

if [[ "$LIST_PORTS" == "1" ]]; then
  "$PIO" device list
  exit 0
fi

if [[ "$TELEMETRY_MODE" != "full" && "$TELEMETRY_MODE" != "compact" && "$TELEMETRY_MODE" != "either" ]]; then
  echo "Invalid --telemetry-mode '$TELEMETRY_MODE' (use full|compact|either)" >&2
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

echo "Using env=$ENV_NAME port=$PORT telemetry_mode=$TELEMETRY_MODE require_nvs_pass=$REQUIRE_NVS_PASS"

echo "[1/5] Build validation firmware"
$PIO run -e "$ENV_NAME"

echo "[2/5] Upload validation firmware"
$PIO run -e "$ENV_NAME" -t upload --upload-port "$PORT"

echo "[3/5] Validate serial markers"
SERIAL_ARGS=(
  --port "$PORT"
  --seconds "$SERIAL_SECONDS"
  --telemetry-mode "$TELEMETRY_MODE"
)
if [[ "$REQUIRE_NVS_PASS" == "1" ]]; then
  SERIAL_ARGS+=(--require-nvs-pass)
fi
$PY "$ROOT_DIR/scripts/serial_validation_check.py" "${SERIAL_ARGS[@]}"

echo "[4/5] Optional HTTP checks"
if [[ "$ENABLE_HTTP_CHECK" == "1" ]]; then
  $PY "$ROOT_DIR/scripts/telemetry_http_check.py" --host "$HOST"
else
  echo "Skipping HTTP checks (no --host provided)"
fi

echo "[5/5] Done"
echo "On-device validation finished successfully."
