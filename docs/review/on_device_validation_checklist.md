# On-Device Validation Checklist (ESP32, Simulated Peripherals)

Date: 2026-02-28

## Scope
Validate runtime behavior on real ESP32 hardware while external devices (Sabertooth/Maestro/MP3/etc.) are simulated or absent.

## What This Validates
1. Firmware boots and runs on target MCU.
2. Serial telemetry is emitted.
3. HTTP telemetry endpoint (`/api/telemetry`) responds.
4. HTTP parameter API (`/api/params`) GET/POST works.
5. WebSocket telemetry endpoint (`/ws/telemetry`) accepts connections (optional if client tool exists).
6. Parameter persistence works across reboot (NVS path on real flash).

## Quick Start (Flash + Runtime Expectations + UI)
1. Pick firmware mode before flashing:
- `esp32dev-validation`: validation-focused path. Use this for serial/controller checks.
- `esp32dev`: runtime path with telemetry enabled.
2. Pick serial format for your consumer:
- `full` (`TEL:{...}`): required if your UI/parser reads telemetry from serial JSON.
- `compact` (`TEL:t=...`): lower-overhead serial diagnostics, not JSON.
- HTTP (`/api/telemetry`) and WebSocket (`/ws/telemetry`) use JSON payloads when telemetry service is enabled.
3. Flash and validate serial markers:
```bash
./scripts/run_on_device_validation.sh --env esp32dev-validation --telemetry-mode full
```
4. Launch UI/API consumer:
- Browser/API client: `http://<device-ip>/api/telemetry` and `http://<device-ip>/api/params`
- WebSocket client: `ws://<device-ip>/ws/telemetry`

## Prerequisites
1. ESP32 board connected over USB.
2. PlatformIO available in shell (`.venv/bin/pio --version`) or via helper script (`scripts/pio_local.sh`).
3. `curl` available.
4. Optional WS client: `websocat` or `wscat`.

## Step 0: Tooling Check
```bash
./scripts/pio_local.sh --version
python3 --version
curl --version
```

If PlatformIO is missing, install into the local venv and use `scripts/pio_local.sh`.

## One-Command Automation (Recommended)
```bash
./scripts/run_on_device_validation.sh --env esp32dev-validation --port /dev/cu.usbserial-XXXX --telemetry-mode full
```

Optional HTTP/API checks once device IP is known:
```bash
./scripts/run_on_device_validation.sh --env esp32dev-validation --port /dev/cu.usbserial-XXXX --host 192.168.1.50 --telemetry-mode full
```

## Step 1: Pick Target Environment
Use one of these from `platformio.ini`:
- `esp32dev`
- `esp32-s3-devkitc-1`
- `esp32-c3-devkitc-02`
- `esp32-c6-devkitc-1`
- `esp32-h2-devkitm-1`

Set env var for convenience:
```bash
export CHOPPER_ENV=esp32dev
```

## Step 2: Discover Serial Port
Preferred (cross-platform):
```bash
./scripts/run_on_device_validation.sh --list-ports
```

macOS example:
```bash
ls /dev/cu.usb* /dev/cu.SLAB* 2>/dev/null
```
Linux example:
```bash
ls /dev/ttyUSB* /dev/ttyACM* 2>/dev/null
```

Set port:
```bash
export CHOPPER_PORT=/dev/cu.usbserial-XXXX
```

## Step 3: Build + Flash
```bash
./scripts/pio_local.sh run -e "$CHOPPER_ENV"
./scripts/pio_local.sh run -e "$CHOPPER_ENV" -t upload --upload-port "$CHOPPER_PORT"
```

## Step 4: Monitor Serial Telemetry
```bash
./scripts/pio_local.sh device monitor --port "$CHOPPER_PORT" --baud 115200
```
Expected:
- Full mode: periodic `TEL:{...}` JSON lines.
- Compact mode: periodic `TEL:t=...` lines.

## Step 5: Find ESP32 IP
From serial logs or router DHCP table, identify `CHOPPER_HOST`.

```bash
export CHOPPER_HOST=192.168.1.50
```

## Step 6: HTTP Telemetry Check
```bash
curl -s "http://$CHOPPER_HOST/api/telemetry"
```
Expected JSON keys include:
- `timestamp_us`
- `loop_count`
- `active_nodes`
- `degradation_mode`

## Step 7: Parameter API Check
List parameters:
```bash
curl -s "http://$CHOPPER_HOST/api/params"
```

Set int parameter:
```bash
curl -s -X POST "http://$CHOPPER_HOST/api/params?name=safety.motor_timeout_ms&value=750"
```

Set float parameter:
```bash
curl -s -X POST "http://$CHOPPER_HOST/api/params?name=drive.max_speed&value=0.55"
```

Set bool parameter:
```bash
curl -s -X POST "http://$CHOPPER_HOST/api/params?name=safety.motor_enabled&value=false"
```

Verify changes:
```bash
curl -s "http://$CHOPPER_HOST/api/params"
```

## Step 8: NVS Persistence Validation
1. Set a known persistent parameter value (for example `drive.max_speed=0.61`).
2. Reboot board (reset button or power cycle).
3. Query `/api/params` again.
4. Confirm value persists after reboot.

Commands:
```bash
curl -s -X POST "http://$CHOPPER_HOST/api/params?name=drive.max_speed&value=0.61"
curl -s "http://$CHOPPER_HOST/api/params" | grep -E 'drive.max_speed'
# reboot board
curl -s "http://$CHOPPER_HOST/api/params" | grep -E 'drive.max_speed'
```

## Step 9: WebSocket Telemetry (Optional)
With `websocat`:
```bash
websocat "ws://$CHOPPER_HOST/ws/telemetry"
```

With `wscat`:
```bash
wscat -c "ws://$CHOPPER_HOST/ws/telemetry"
```

Expected: periodic JSON telemetry messages.

## Pass/Fail Recording
Capture results in:
- `docs/review/hil_results_YYYY-MM-DD.md`

Record:
1. Board and env used
2. Serial telemetry pass/fail
3. HTTP telemetry pass/fail
4. Parameter API pass/fail
5. NVS persistence pass/fail
6. WebSocket pass/fail (if tested)
