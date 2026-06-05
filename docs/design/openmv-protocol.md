# OpenMV ↔ ESP32 Serial Protocol

Binary framing protocol for communication between the ESP32 main controller
and the OpenMV camera module over UART2 (115200 baud).

## Wire Format

Every message uses the same frame structure:

```
┌──────┬────────┬─────┬────────────────┬──────────┐
│ SYNC │ CMD_ID │ LEN │ PAYLOAD[0..N]  │ CHECKSUM │
│ 0xA5 │ 1 byte │ 1 B │ 0–16 bytes     │ 1 byte   │
└──────┴────────┴─────┴────────────────┴──────────┘
```

- **SYNC**: `0xA5` — frame start marker
- **CMD_ID**: Command identifier (see tables below)
- **LEN**: Payload length in bytes (0–16)
- **PAYLOAD**: Command-specific data
- **CHECKSUM**: `CMD_ID ^ LEN ^ PAYLOAD[0] ^ ... ^ PAYLOAD[N-1]`

### Error Handling

- Bad checksum → frame silently discarded
- Payload length > 16 → frame discarded, parser resets
- Unrecognized CMD_ID → frame discarded
- Garbage bytes between frames → ignored (parser waits for next `0xA5`)

## ESP32 → OpenMV Commands (LED Control)

| CMD_ID | Name             | LEN | Payload                          |
|--------|------------------|-----|----------------------------------|
| `0x01` | LED_SET_COLOR    | 5   | `led_id, red, green, blue, white`|
| `0x02` | LED_SET_BRIGHTNESS| 2  | `led_id, brightness`             |
| `0x03` | LED_SET_PATTERN  | 2   | `led_id, pattern_id`             |
| `0x04` | LED_OFF          | 1   | `led_id`                         |
| `0x05` | LED_ON           | 1   | `led_id`                         |
| `0x10` | TRACKING_SET     | 1   | `enabled`                        |

### Field Descriptions

| Field       | Type   | Range   | Description                        |
|-------------|--------|---------|------------------------------------|
| led_id      | uint8  | 0–255   | LED group identifier               |
| red         | uint8  | 0–255   | Red channel intensity              |
| green       | uint8  | 0–255   | Green channel intensity            |
| blue        | uint8  | 0–255   | Blue channel intensity             |
| white       | uint8  | 0–255   | White channel intensity            |
| brightness  | uint8  | 0–255   | Global brightness level            |
| pattern_id  | uint8  | 0–255   | Animation pattern identifier       |
| enabled     | uint8  | 0–1     | 1 = start face tracking, 0 = stop  |

### LED IDs

| LED ID | Target |
|--------|--------|
| `1` | Right eye NeoPixel strip |
| `2` | Center/left eye NeoPixel strip |
| `4` | Periscope NeoPixel |

The ESP32 sends blue `LED_SET_COLOR` for LED IDs `1` and `2` when `DomeNode` activates.
The ESP32 sends blue `LED_SET_COLOR` for LED ID `4` after the periscope lift delay completes and `LED_OFF` when the periscope starts lowering.

## OpenMV → ESP32 Commands (Vision)

| CMD_ID | Name           | LEN | Payload                                              |
|--------|----------------|-----|------------------------------------------------------|
| `0x80` | VISION_RESULT  | 10  | `cx_hi, cx_lo, cy_hi, cy_lo, w_hi, w_lo, h_hi, h_lo, confidence, detected` |

### VISION_RESULT Fields

All multi-byte values are **big-endian**.

| Field      | Type    | Bytes | Description                                         |
|------------|---------|-------|-----------------------------------------------------|
| center_x   | int16   | 0–1   | Bounding box center X (pixels, signed, 0 = frame center) |
| center_y   | int16   | 2–3   | Bounding box center Y (pixels, signed, 0 = frame center) |
| width      | uint16  | 4–5   | Bounding box width (pixels)                         |
| height     | uint16  | 6–7   | Bounding box height (pixels)                        |
| confidence | uint8   | 8     | Detection confidence (0–255)                        |
| detected   | uint8   | 9     | 1 = face detected, 0 = no detection                 |

**Coordinate system**: Origin (0, 0) is the center of the camera frame.
Positive X is right, positive Y is down. For a 320×240 frame, valid
center_x range is approximately -160 to +160.

## MicroPython Reference (OpenMV Side)

```python
import struct
from machine import UART

SYNC = 0xA5

# ── Receiving commands from ESP32 ──

CMD_LED_SET_COLOR      = 0x01
CMD_LED_SET_BRIGHTNESS = 0x02
CMD_LED_SET_PATTERN    = 0x03
CMD_LED_OFF            = 0x04
CMD_LED_ON             = 0x05
CMD_TRACKING_SET       = 0x10

def read_frame(uart):
    """Read and parse one frame. Returns (cmd_id, payload) or None."""
    # Wait for sync byte
    while True:
        b = uart.read(1)
        if b is None:
            return None
        if b[0] == SYNC:
            break

    header = uart.read(2)
    if header is None or len(header) < 2:
        return None
    cmd_id, length = header[0], header[1]
    if length > 16:
        return None

    payload = uart.read(length) if length > 0 else b''
    if payload is None or len(payload) < length:
        return None

    cs_byte = uart.read(1)
    if cs_byte is None:
        return None

    expected = cmd_id ^ length
    for b in payload:
        expected ^= b
    if cs_byte[0] != (expected & 0xFF):
        return None

    return cmd_id, payload


tracking_enabled = False

def handle_command(cmd_id, payload):
    """Dispatch a command from ESP32."""
    global tracking_enabled
    if cmd_id == CMD_TRACKING_SET and len(payload) >= 1:
        tracking_enabled = payload[0] != 0
        # Start/stop face detection loop based on tracking_enabled
    elif cmd_id == CMD_LED_SET_COLOR and len(payload) >= 5:
        led_id, r, g, b, w = payload[0], payload[1], payload[2], payload[3], payload[4]
        # TODO: set LED color
    elif cmd_id == CMD_LED_SET_BRIGHTNESS and len(payload) >= 2:
        led_id, brightness = payload[0], payload[1]
        # TODO: set LED brightness
    elif cmd_id == CMD_LED_SET_PATTERN and len(payload) >= 2:
        led_id, pattern_id = payload[0], payload[1]
        # TODO: start LED pattern
    elif cmd_id == CMD_LED_OFF and len(payload) >= 1:
        led_id = payload[0]
        # TODO: turn off LED
    elif cmd_id == CMD_LED_ON and len(payload) >= 1:
        led_id = payload[0]
        # TODO: turn on LED


# ── Sending vision results to ESP32 ──

CMD_VISION_RESULT = 0x80

def send_vision_result(uart, center_x, center_y, width, height, confidence, detected):
    """Send a face detection bounding box to the ESP32."""
    payload = struct.pack('>hhHHBB', center_x, center_y, width, height,
                          confidence, 1 if detected else 0)
    cs = CMD_VISION_RESULT ^ len(payload)
    for b in payload:
        cs ^= b
    uart.write(bytes([SYNC, CMD_VISION_RESULT, len(payload)]) + payload + bytes([cs & 0xFF]))
```

## ESP32 Pub/Sub Topics

| Topic                 | Message Type    | Direction                      | Description                        |
|-----------------------|-----------------|--------------------------------|------------------------------------|
| `led/dome_eye/cmd`    | LEDCommand      | → OpenMvBridgeNode → serial    | OpenMV LED commands                |
| `openmv/tracking/cmd` | TrackingCommand | DomeNode → OpenMvBridgeNode → serial | Enable/disable face detection |
| `vision/result`       | VisionResult    | serial → OpenMvBridgeNode → DomeNode | Face detection bounding box   |

## Hardware

| Parameter | Value              |
|-----------|--------------------|
| UART      | UART2 (HW)        |
| TX pin    | GPIO25             |
| RX pin    | GPIO33             |
| Baud      | 115200             |
| Config    | 8N1                |
