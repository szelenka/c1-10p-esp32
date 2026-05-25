# Telemetry UI

Lightweight local app that reads serial `TEL:` telemetry and visualizes:
- Controller button activity (mask-based)
- Motor events
- Servo events (grouped by `group` when provided, e.g. `body`/`dome`)
- LED events
- Audio status
- 3D robot view (fallback model + optional URDF loader)

## Files

- `app.py`: serial bridge + HTTP/WebSocket server
- `web/index.html`: UI layout
- `web/app.js`: live rendering and websocket client
- `web/styles.css`: UI styling

## Supported `TEL:` formats

1. Compact key/value payload (hardware default)
- Reads fields like `drv=1`, `dome=1`, `d_btn=0x0003`, `m_btn=0x0004`, `d_dpad=...`, `m_dpad=...`
- Reads compact axes like `d_ax=(0,0,-409,0)` and grouped servo keys like `sb3=1500.0`, `sd1=1282.0`
- Reads one-frame audio events like `sound_type=0 track=254` for sound notifications
- Also tolerates compact event keys such as `motor0=0.6 servo2=1500 ledstatus=on`

2. JSON payload (diagnostic/full format)
- Example: `TEL:{"inputs":{"drive":{"buttons":3},"dome":{"buttons":4}},"outputs":{"motors":[...],"servos":[...],"leds":[...]}}`

## Install

```bash
cd tools/telemetry_ui
python3 -m venv .venv
source .venv/bin/activate
pip install -r requirements.txt
```

## Run

```bash
python app.py --serial /dev/ttyUSB0 --baud 115200 --host 127.0.0.1 --port 8765
```

Then open `http://127.0.0.1:8765`.

## 3D Renderer

The UI now includes a live 3D viewport:
- Default mode: fallback robot mesh rendered with Three.js.
- Servo telemetry drives visible parts (dome spin, periscope lift, body/dome doors).

### URDF mode

Pass a URDF URL in the page query string:

```text
http://127.0.0.1:8765/?urdf=http://127.0.0.1:8000/robot.urdf
```

Optional package root for mesh resolution:

```text
http://127.0.0.1:8765/?urdf=http://127.0.0.1:8000/robot.urdf&pkg=http://127.0.0.1:8000/
```

If URDF loading fails, the viewport automatically falls back to the built-in model.

### No serial attached

You can start the UI without serial input:

```bash
python app.py --port 8765
```

The page loads and waits for telemetry connections.
