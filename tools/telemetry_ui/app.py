#!/usr/bin/env python3
import argparse
import asyncio
import json
import logging
import math
import os
import re
import signal
import time
from pathlib import Path
from typing import Any, Dict, List, Optional

import serial
from aiohttp import web

LOG = logging.getLogger("telemetry_ui")

LEFT_BUTTON_LABELS = {
    0: "Down",
    1: "Right",
    2: "Left",
    3: "Up",
    4: "SL",
    5: "SR",
    6: "L",
    7: "ZL",
    8: "Stick",
}

RIGHT_BUTTON_LABELS = {
    0: "B",
    1: "A",
    2: "Y",
    3: "X",
    4: "SL",
    5: "SR",
    6: "R",
    7: "ZR",
    8: "Stick",
}

LEFT_MISC_LABELS = {
    0: "System",
    1: "Minus",
    2: "Capture",
}

RIGHT_MISC_LABELS = {
    0: "System",
    1: "Home",
    2: "Plus",
}

class TelemetryParser:
    KEYVAL_RE = re.compile(r"([A-Za-z_][A-Za-z0-9_]*)=([^\s]+)")
    SERVO_COMMANDS = {
        0: "position",
        1: "speed",
        2: "disable",
        3: "enable",
    }

    def parse_line(self, line: str) -> Optional[Dict[str, Any]]:
        idx = line.find("TEL:")
        if idx < 0:
            return None

        payload = line[idx + 4 :].strip()
        if not payload:
            return None

        if payload.startswith("{"):
            try:
                obj = json.loads(payload)
            except json.JSONDecodeError:
                return {
                    "kind": "parse_error",
                    "format": "json",
                    "received_at": time.time(),
                    "raw": payload,
                    "error": "invalid_json",
                }
            return self._normalize_json(obj, payload)

        return self._normalize_compact(payload)

    # Slot definitions: (role, type, compact_prefix)
    ROLE_SLOTS = [
        ("drive", "left", "d"),
        ("dome", "right", "m"),
        ("animation", "left", "a"),
        ("camera", "right", "c"),
    ]

    def _extract_input_controller(
        self, obj: Dict[str, Any], inputs: Dict[str, Any], role: str, jc_type: str
    ) -> Dict[str, Any]:
        """Extract a single controller's data from JSON telemetry."""
        source = inputs.get(role) or obj.get(role) or {}
        connected_val = self._pick_int(source, ["connected", "conn", "is_connected", "valid"])
        buttons = self._pick_int(source, ["buttons", "btn_mask", "button_mask"])
        misc = self._pick_int(source, ["misc", "misc_buttons"])
        axes = self._extract_axes(source)
        player_leds = self._pick_int(source, ["player_leds", "leds_mask"])
        battery = self._pick_int(source, ["battery", "battery_level"])
        has_data_val = self._pick_int(source, ["has_data", "data"])
        avg_interval_us = self._pick_int(source, ["avg_interval_us", "avg_report_interval_us"])
        labels = LEFT_BUTTON_LABELS if jc_type == "left" else RIGHT_BUTTON_LABELS
        misc_labels = LEFT_MISC_LABELS if jc_type == "left" else RIGHT_MISC_LABELS
        return {
            "role": role,
            "type": jc_type,
            "connected": bool(connected_val) if connected_val is not None else False,
            "has_data": bool(has_data_val) if has_data_val is not None else False,
            "buttons": buttons or 0,
            "misc": misc or 0,
            "axes": axes,
            "player_leds": player_leds,
            "battery": battery,
            "avg_interval_us": avg_interval_us,
            "pressed": self._mask_to_labels(buttons, labels)
            + self._mask_to_labels(misc, misc_labels),
        }

    def _normalize_json(self, obj: Dict[str, Any], raw: str) -> Dict[str, Any]:
        inputs = obj.get("inputs") or {}
        outputs = obj.get("outputs") or {}

        controllers = []
        for role, jc_type, _ in self.ROLE_SLOTS:
            controllers.append(self._extract_input_controller(obj, inputs, role, jc_type))

        motors = self._normalize_motors(outputs.get("motors") or obj.get("motors") or [])
        servos = self._normalize_servos(outputs.get("servos") or obj.get("servos") or [])
        leds = self._normalize_leds(
            outputs.get("leds")
            or outputs.get("led")
            or obj.get("leds")
            or obj.get("led")
            or []
        )
        audio = self._normalize_audio(outputs.get("audio") or obj.get("audio"))

        return {
            "kind": "telemetry",
            "format": "json",
            "received_at": time.time(),
            "source_timestamp_us": self._pick_int(obj, ["timestamp_us", "t"]),
            "raw": raw,
            "controllers": controllers,
            "motors": motors,
            "servos": servos,
            "leds": leds,
            "audio": audio,
        }

    def _normalize_compact(self, payload: str) -> Dict[str, Any]:
        fields = {m.group(1): m.group(2) for m in self.KEYVAL_RE.finditer(payload)}

        # Build controllers from compact prefixes
        controllers = []
        for role, jc_type, prefix in self.ROLE_SLOTS:
            btn_val = self._parse_number(fields.get(f"{prefix}_btn"))
            edge_val = self._parse_number(fields.get(f"{prefix}_edge"))
            conn_val = self._parse_number(fields.get(f"{prefix}_conn"))
            if conn_val is None and role == "drive":
                conn_val = self._parse_number(fields.get("drv"))
            if conn_val is None and role == "dome":
                conn_val = self._parse_number(fields.get("dome"))
            misc_val = self._parse_number(fields.get(f"{prefix}_misc"))
            misc_edge_val = self._parse_number(fields.get(f"{prefix}_medge"))
            axes = self._extract_compact_axes(payload, f"{prefix}_ax")
            labels = LEFT_BUTTON_LABELS if jc_type == "left" else RIGHT_BUTTON_LABELS
            misc_labels = LEFT_MISC_LABELS if jc_type == "left" else RIGHT_MISC_LABELS
            btn_int = int(btn_val) if btn_val is not None else 0
            edge_int = int(edge_val) if edge_val is not None else 0
            misc_int = int(misc_val) if misc_val is not None else 0
            misc_edge_int = int(misc_edge_val) if misc_edge_val is not None else 0
            controllers.append({
                "role": role,
                "type": jc_type,
                "connected": bool(conn_val) if conn_val is not None else False,
                "has_data": True if conn_val else False,
                "buttons": btn_int,
                "misc": misc_int,
                "button_edge_mask": edge_int,
                "misc_edge_mask": misc_edge_int,
                "axes": axes,
                "player_leds": None,
                "pressed": self._mask_to_labels(btn_int, labels)
                + self._mask_to_labels(edge_int, labels)
                + self._mask_to_labels(misc_int, misc_labels)
                + self._mask_to_labels(misc_edge_int, misc_labels),
            })

        motors: List[Dict[str, Any]] = []
        servos: List[Dict[str, Any]] = []
        leds: List[Dict[str, Any]] = []
        audio: Optional[Dict[str, Any]] = None

        for key, raw_value in fields.items():
            value = self._parse_number(raw_value)
            low = key.lower()

            m = re.match(r"(?:motor|m)(\d+)$", low)
            if m and low not in {"m_btn", "mode"} and value is not None:
                motors.append({"id": int(m.group(1)), "value": float(value), "type": None})
                continue

            s = re.match(r"(?:servo|s)(a|b|d)?(\d+)$", low)
            if s and value is not None:
                group_char = s.group(1)
                group_map = {"a": "any", "b": "body", "d": "dome"}
                command_type = None
                servos.append(
                    {
                        "id": int(s.group(2)),
                        "value": float(value),
                        "type": command_type,
                        "command": self._servo_command_name(command_type),
                        "dur_ms": None,
                        "group": group_map.get(group_char) if group_char else None,
                    }
                )
                continue

            l = re.match(r"(?:led|light)([A-Za-z0-9_]+)$", low)
            if l:
                leds.append(
                    {
                        "id": l.group(1),
                        "value": raw_value,
                        "state": self._normalize_led_state(raw_value),
                        "color": self._extract_led_color(raw_value),
                    }
                )
                continue

            if low in {"audio_type", "sound_type"}:
                if audio is None:
                    audio = {}
                if value is not None:
                    audio["type"] = int(value)
                continue

            if low in {"audio_track", "sound_track", "track"}:
                if audio is None:
                    audio = {}
                if value is not None:
                    audio["track"] = int(value)
                continue

            if low in {"audio_volume", "sound_volume"}:
                if audio is None:
                    audio = {}
                if value is not None:
                    audio["volume"] = int(value)
                continue

            if low in {"audio_loop", "sound_loop"}:
                if audio is None:
                    audio = {}
                if value is not None:
                    audio["loop"] = bool(int(value))
                continue

        return {
            "kind": "telemetry",
            "format": "compact",
            "received_at": time.time(),
            "source_timestamp_us": self._parse_number(fields.get("t")),
            "raw": payload,
            "controllers": controllers,
            "motors": motors,
            "servos": servos,
            "leds": leds,
            "audio": audio,
        }

    def _normalize_motors(self, motors: Any) -> List[Dict[str, Any]]:
        out = []
        for item in motors if isinstance(motors, list) else []:
            if not isinstance(item, dict):
                continue
            motor_id = self._pick_int(item, ["id", "motor_id"])
            if motor_id is None:
                continue
            out.append(
                {
                    "id": motor_id,
                    "value": self._pick_float(item, ["value", "speed", "power"]),
                    "type": self._pick_int(item, ["type", "command_type"]),
                }
            )
        return out

    def _normalize_servos(self, servos: Any) -> List[Dict[str, Any]]:
        out = []
        for item in servos if isinstance(servos, list) else []:
            if not isinstance(item, dict):
                continue
            servo_id = self._pick_int(item, ["id", "servo_id"])
            if servo_id is None:
                continue
            command_type = self._pick_int(item, ["type", "command_type"])
            out.append(
                {
                    "id": servo_id,
                    "value": self._pick_float(item, ["value", "position", "pulse"]),
                    "type": command_type,
                    "command": self._servo_command_name(command_type),
                    "dur_ms": self._pick_int(item, ["dur_ms", "duration_ms"]),
                    "group": item.get("group") or item.get("cluster") or item.get("bank"),
                    "name": item.get("name") or item.get("label"),
                }
            )
        return out

    def _normalize_leds(self, leds: Any) -> List[Dict[str, Any]]:
        if isinstance(leds, dict):
            leds = [leds]
        out = []
        for idx, item in enumerate(leds if isinstance(leds, list) else []):
            if isinstance(item, dict):
                led_id = item.get("id")
                if led_id is None:
                    led_id = item.get("name") or item.get("index") or idx
                out.append(
                    {
                        "id": str(led_id),
                        "state": item.get("state") or item.get("on"),
                        "color": item.get("color") or item.get("rgb") or item.get("hex"),
                        "brightness": item.get("brightness"),
                        "value": item.get("value"),
                    }
                )
            else:
                out.append(
                    {
                        "id": str(idx),
                        "state": item,
                        "color": self._extract_led_color(item),
                        "value": item,
                    }
                )
        return out

    def _normalize_audio(self, audio: Any) -> Optional[Dict[str, Any]]:
        if not isinstance(audio, dict):
            return None
        valid_raw = audio.get("valid")
        valid_num = self._parse_number(valid_raw)
        if valid_num is not None:
            valid_bool = bool(int(valid_num))
        elif isinstance(valid_raw, bool):
            valid_bool = valid_raw
        else:
            valid_bool = None

        loop_raw = audio.get("loop")
        loop_num = self._parse_number(loop_raw)
        if loop_num is not None:
            loop_bool = bool(int(loop_num))
        elif isinstance(loop_raw, bool):
            loop_bool = loop_raw
        else:
            loop_bool = None
        return {
            "valid": valid_bool,
            "type": self._pick_int(audio, ["type", "command_type", "cmd"]),
            "track": self._pick_int(audio, ["track", "track_id", "id"]),
            "volume": self._pick_int(audio, ["volume", "vol"]),
            "loop": loop_bool,
        }

    @staticmethod
    def _parse_number(value: Any) -> Optional[float]:
        if value is None:
            return None
        if isinstance(value, (int, float)):
            return float(value)
        if isinstance(value, str):
            val = value.strip().rstrip(",")
            if not val:
                return None
            try:
                if val.lower().startswith("0x"):
                    return float(int(val, 16))
                return float(val)
            except ValueError:
                return None
        return None

    def _pick_int(self, obj: Dict[str, Any], keys: List[str], fallback: Optional[float] = None) -> Optional[int]:
        for key in keys:
            if key in obj:
                parsed = self._parse_number(obj.get(key))
                if parsed is not None:
                    return int(parsed)
        if fallback is not None:
            return int(fallback)
        return None

    def _pick_float(self, obj: Dict[str, Any], keys: List[str]) -> Optional[float]:
        for key in keys:
            if key in obj:
                parsed = self._parse_number(obj.get(key))
                if parsed is not None:
                    return float(parsed)
        return None

    def _servo_command_name(self, command_type: Optional[int]) -> str:
        if command_type is None:
            return "position"
        return self.SERVO_COMMANDS.get(command_type, "unknown")

    def _extract_axes(self, input_obj: Dict[str, Any]) -> Optional[List[float]]:
        axes = input_obj.get("axes")
        if isinstance(axes, list) and len(axes) >= 4:
            return [float(v) if v is not None else 0.0 for v in axes[:4]]
        ax = self._parse_number(input_obj.get("axis_x"))
        ay = self._parse_number(input_obj.get("axis_y"))
        arx = self._parse_number(input_obj.get("axis_rx"))
        ary = self._parse_number(input_obj.get("axis_ry"))
        if any(v is not None for v in [ax, ay, arx, ary]):
            return [float(v or 0) for v in [ax, ay, arx, ary]]
        return None

    AXES_RE = re.compile(r"(\w+)=\((-?\d+),(-?\d+),(-?\d+),(-?\d+)\)")

    def _extract_compact_axes(self, payload: str, prefix: str) -> Optional[List[float]]:
        for m in self.AXES_RE.finditer(payload):
            if m.group(1) == prefix:
                return [float(m.group(i)) for i in range(2, 6)]
        return None

    @staticmethod
    def _mask_to_labels(mask, labels: Dict[int, str]) -> List[str]:
        if mask is None:
            return []
        mask = int(mask)
        pressed = []
        for bit, label in labels.items():
            if mask & (1 << bit):
                pressed.append(label)
        return pressed

    @staticmethod
    def _normalize_led_state(value: Any) -> Any:
        if isinstance(value, str):
            low = value.lower()
            if low in {"1", "on", "true", "enabled"}:
                return True
            if low in {"0", "off", "false", "disabled"}:
                return False
        return value

    @staticmethod
    def _extract_led_color(value: Any) -> Optional[str]:
        if isinstance(value, str):
            v = value.strip()
            if re.match(r"^#?[0-9A-Fa-f]{6}$", v):
                return v if v.startswith("#") else f"#{v}"
            if v.lower().startswith("0x") and len(v) in {8, 10}:
                return f"#{v[2:8]}"
        return None


class TelemetryBridge:
    def __init__(self, serial_port: Optional[str], baud: int, reconnect_s: float, web_dir: Path, simulate: bool = False):
        self.serial_port = serial_port
        self.baud = baud
        self.reconnect_s = reconnect_s
        self.web_dir = web_dir
        self.simulate = simulate
        self.parser = TelemetryParser()
        self.clients: set[web.WebSocketResponse] = set()
        self._task: Optional[asyncio.Task] = None
        self._running = False

    async def start(self) -> None:
        if self._running:
            return
        self._running = True
        if self.simulate:
            self._task = asyncio.create_task(self._simulate_loop(), name="simulate-loop")
        else:
            self._task = asyncio.create_task(self._serial_loop(), name="serial-loop")

    async def stop(self) -> None:
        self._running = False
        if self._task:
            self._task.cancel()
            try:
                await self._task
            except asyncio.CancelledError:
                pass
            self._task = None

    def _status_payload(self, connected: bool, message: str, **extra) -> Dict[str, Any]:
        mode = "simulate" if self.simulate else ("serial" if self.serial_port else "none")
        payload: Dict[str, Any] = {
            "kind": "status",
            "connected": connected,
            "mode": mode,
            "message": message,
            "received_at": time.time(),
        }
        if self.serial_port:
            payload["serial_port"] = self.serial_port
        payload.update(extra)
        return payload

    async def ws_handler(self, request: web.Request) -> web.WebSocketResponse:
        ws = web.WebSocketResponse(heartbeat=20)
        await ws.prepare(request)
        self.clients.add(ws)
        if self.simulate:
            await ws.send_json(self._status_payload(True, "Simulation mode active"))
        elif self.serial_port:
            await ws.send_json(self._status_payload(False, f"Serial: {self.serial_port}"))
        else:
            await ws.send_json(self._status_payload(False, "No serial port configured"))

        try:
            async for msg in ws:
                if msg.type == web.WSMsgType.TEXT and msg.data.strip().lower() == "ping":
                    await ws.send_str("pong")
        finally:
            self.clients.discard(ws)
        return ws

    async def broadcast(self, payload: Dict[str, Any]) -> None:
        if not self.clients:
            return

        dead: List[web.WebSocketResponse] = []
        for ws in self.clients:
            if ws.closed:
                dead.append(ws)
                continue
            try:
                await ws.send_json(payload)
            except Exception:
                dead.append(ws)

        for ws in dead:
            self.clients.discard(ws)

    async def _serial_loop(self) -> None:
        if not self.serial_port:
            await self.broadcast(self._status_payload(False, "No serial port configured"))
            while self._running:
                await asyncio.sleep(1.0)
            return

        while self._running:
            try:
                await self.broadcast(self._status_payload(
                    False, f"Opening {self.serial_port} @ {self.baud}"
                ))
                with serial.Serial(self.serial_port, self.baud, timeout=0.2) as ser:
                    LOG.info("Serial connected: %s @ %d", self.serial_port, self.baud)
                    await self.broadcast(self._status_payload(
                        True, "Receiving telemetry",
                    ))

                    while self._running:
                        raw = await asyncio.to_thread(ser.readline)
                        if not raw:
                            continue

                        line = raw.decode("utf-8", errors="replace").strip()
                        if "TEL:" not in line:
                            print(line, flush=True)
                            continue
                        event = self.parser.parse_line(line)
                        if not event:
                            continue

                        event["line"] = line
                        await self.broadcast(event)

            except (serial.SerialException, OSError) as exc:
                LOG.warning("Serial error: %s", exc)
                await self.broadcast(self._status_payload(
                    False, f"Serial error: {exc}"
                ))
                await asyncio.sleep(self.reconnect_s)

    async def _simulate_loop(self) -> None:
        """Generate synthetic telemetry at ~10 Hz so the 3D model animates.

        Mirrors firmware behavior: dome spin is driven by L2 button presses
        on the drive (rotate left) and dome (rotate right) controllers,
        NOT by joystick axes.
        """
        await self.broadcast(self._status_payload(True, "Simulation active"))
        LOG.info("Simulation mode active (10 Hz)")
        t0 = time.monotonic()
        interval = 0.1  # 10 Hz

        # Button bit positions (Joy-Con mapping)
        L2_BIT = 7   # ZL on left Joy-Con / ZR on right Joy-Con

        while self._running:
            t = time.monotonic() - t0

            # Simulate dome rotation via button presses:
            # Drive controller L2 = rotate left, Dome controller L2 = rotate right
            # Alternate: 4s left, 2s idle, 4s right, 2s idle
            dome_cycle = t % 12.0
            drive_l2_pressed = dome_cycle < 4.0
            dome_l2_pressed = 6.0 <= dome_cycle < 10.0

            dome_speed = 0.0
            if drive_l2_pressed and not dome_l2_pressed:
                dome_speed = 0.5   # rotate left
            elif dome_l2_pressed and not drive_l2_pressed:
                dome_speed = -0.5  # rotate right

            drive_btn_mask = (1 << L2_BIT) if drive_l2_pressed else 0
            dome_btn_mask = (1 << L2_BIT) if dome_l2_pressed else 0

            # Motors: gentle wheel drift + button-driven dome spin
            motors = [
                {"id": 0, "type": 0, "value": round(0.3 * math.sin(t * 0.5), 3)},
                {"id": 1, "type": 0, "value": round(0.3 * math.sin(t * 0.5), 3)},
                {"id": 2, "type": 0, "value": round(dome_speed, 3)},
            ]

            # Body servos — PWM values match DefaultParameters.h calibration
            # Servo sim helper: oscillate between neutral and max
            def servo_sim(neutral, mn, mx, phase):
                half_range = (mx - mn) / 2
                return round(neutral + half_range * math.sin(phase), 1)

            def servo_extend(neutral, mx, phase):
                """Extend from neutral toward max on positive half of sine."""
                return round(neutral + (mx - neutral) * max(0, math.sin(phase)), 1)

            body_servos = [
                # Neck legs: gentle tilt within calibrated range
                {"id": 0, "group": "body", "type": 0, "value": servo_sim(2256, 2032, 2256, t * 0.7), "dur_ms": 0},
                {"id": 1, "group": "body", "type": 0, "value": servo_sim(2176, 1952, 2176, t * 0.7 + 2.09), "dur_ms": 0},
                {"id": 2, "group": "body", "type": 0, "value": servo_sim(2272, 2048, 2272, t * 0.7 + 4.19), "dur_ms": 0},
                # Utility arm: extend from neutral(1264) toward max(2384)
                {"id": 3, "group": "body", "type": 0, "value": servo_extend(1264, 2384, t * 0.4), "dur_ms": 0},
                # Body doors: open from neutral toward min (right) / max (left)
                {"id": 4, "group": "body", "type": 0, "value": servo_extend(1920, 992, t * 0.3), "dur_ms": 0},
                {"id": 5, "group": "body", "type": 0, "value": servo_extend(1030, 1696, t * 0.3), "dur_ms": 0},
            ]

            # Dome servos — PWM values match DefaultParameters.h calibration
            dome_servos = [
                # Periscope lift: rise from neutral(800) toward max(1744)
                {"id": 0, "group": "dome", "type": 0, "value": servo_extend(800, 1744, t * 0.35), "dur_ms": 0},
                # Periscope spin: oscillate around neutral(1282) within [496, 2496]
                {"id": 1, "group": "dome", "type": 0, "value": servo_sim(1282, 496, 2496, t * 0.6), "dur_ms": 0},
                # Dome doors: open from neutral toward min
                {"id": 2, "group": "dome", "type": 0, "value": servo_extend(2304, 496, t * 0.25), "dur_ms": 0},
                {"id": 6, "group": "dome", "type": 0, "value": servo_extend(576, 2496, t * 0.25 + 1.57), "dur_ms": 0},
                # Right arm
                {"id": 3, "group": "dome", "type": 0, "value": round(1500 + 300 * math.sin(t * 0.45), 1), "dur_ms": 0},
                {"id": 4, "group": "dome", "type": 0, "value": round(1500 + 400 * max(0, math.sin(t * 0.5)), 1), "dur_ms": 0},
                {"id": 5, "group": "dome", "type": 0, "value": round(1500 + 400 * math.sin(t * 0.8), 1), "dur_ms": 0},
                # Left arm
                {"id": 7, "group": "dome", "type": 0, "value": round(1500 + 300 * math.sin(t * 0.45 + 3.14), 1), "dur_ms": 0},
                {"id": 8, "group": "dome", "type": 0, "value": round(1500 + 400 * max(0, math.sin(t * 0.5 + 1.57)), 1), "dur_ms": 0},
                {"id": 9, "group": "dome", "type": 0, "value": round(1500 + 400 * math.sin(t * 0.8 + 3.14), 1), "dur_ms": 0},
            ]

            # LEDs: cycle RGB colors
            led_r = int(127.5 + 127.5 * math.sin(t * 1.0))
            led_g = int(127.5 + 127.5 * math.sin(t * 1.0 + 2.09))
            led_b = int(127.5 + 127.5 * math.sin(t * 1.0 + 4.19))
            leds = [
                {"id": "0", "state": "on", "color": {"r": led_r, "g": led_g, "b": led_b}},
                {"id": "1", "state": "on", "color": {"r": led_b, "g": led_r, "b": led_g}},
                {"id": "2", "state": "on", "color": {"r": led_g, "g": led_b, "b": led_r}},
                {"id": "3", "state": "on", "color": {"r": led_r, "g": led_b, "b": led_g}},
            ]

            event = {
                "kind": "telemetry",
                "format": "simulate",
                "received_at": time.time(),
                "source_timestamp_us": int(t * 1_000_000),
                "controllers": [
                    {
                        "role": "drive",
                        "type": "left",
                        "connected": True,
                        "buttons": drive_btn_mask,
                        "misc": 0,
                        "axes": [
                            round(512 * math.sin(t * 0.8), 0),
                            round(512 * math.cos(t * 0.6), 0),
                            0, 0,
                        ],
                        "player_leds": 0x0F,
                        "pressed": [],
                    },
                    {
                        "role": "dome",
                        "type": "right",
                        "connected": True,
                        "buttons": dome_btn_mask,
                        "misc": 0,
                        "axes": [0, 0, 0, 0],
                        "player_leds": 0x0F,
                        "pressed": [],
                    },
                    {
                        "role": "animation",
                        "type": "left",
                        "connected": t > 3,
                        "buttons": 0,
                        "misc": 0,
                        "axes": [
                            round(256 * math.sin(t * 0.3), 0),
                            round(256 * math.cos(t * 0.4), 0),
                            0, 0,
                        ],
                        "player_leds": 0x03 if t > 3 else 0,
                        "pressed": [],
                    },
                    {
                        "role": "camera",
                        "type": "right",
                        "connected": t > 5,
                        "buttons": 0,
                        "misc": 0,
                        "axes": [
                            round(256 * math.cos(t * 0.2), 0),
                            round(256 * math.sin(t * 0.3), 0),
                            0, 0,
                        ],
                        "player_leds": 0x01 if t > 5 else 0,
                        "pressed": [],
                    },
                ],
                "motors": motors,
                "servos": body_servos + dome_servos,
                "leds": leds,
                "audio": None,
            }

            await self.broadcast(event)
            await asyncio.sleep(interval)


def parse_servo_calibration() -> Dict[str, Dict[str, int]]:
    """Parse DefaultParameters.h to extract servo min/max/neutral values.

    Returns a dict like {"body:0": {"min": 2032, "max": 2256, "neutral": 2256}, ...}.
    Maps parameter names to group:channel using the naming convention in HardwareConfig.h.
    """
    # Parameter name prefix → (group, channel)
    SERVO_PARAM_MAP = {
        "servo.neck_a":    ("body", 0),
        "servo.neck_b":    ("body", 1),
        "servo.neck_c":    ("body", 2),
        "servo.util_arm":  ("body", 3),
        "servo.bdoor_r":   ("body", 4),
        "servo.bdoor_l":   ("body", 5),
        "servo.peri_lift":  ("dome", 0),
        "servo.peri_spin":  ("dome", 1),
        "servo.ddoor_r":    ("dome", 2),
        "servo.ddoor_l":    ("dome", 6),
    }

    defaults_path = Path(__file__).resolve().parent.parent.parent / "main" / "include" / "chopper" / "config" / "DefaultParameters.h"
    if not defaults_path.is_file():
        LOG.warning("DefaultParameters.h not found at %s", defaults_path)
        return {}

    text = defaults_path.read_text()

    # Match: ps.declare("servo.xxx.min", static_cast<int32_t>(VALUE), ...)
    declare_re = re.compile(
        r'ps\.declare\(\s*"([^"]+)"\s*,\s*static_cast<int32_t>\((\d+)\)'
    )

    # Collect raw param values
    raw: Dict[str, int] = {}
    for m in declare_re.finditer(text):
        raw[m.group(1)] = int(m.group(2))

    # Build calibration map
    result: Dict[str, Dict[str, int]] = {}
    for prefix, (group, channel) in SERVO_PARAM_MAP.items():
        key = f"{group}:{channel}"
        mn = raw.get(f"{prefix}.min")
        mx = raw.get(f"{prefix}.max")
        neutral = raw.get(f"{prefix}.neutral")
        if mn is not None and mx is not None and neutral is not None:
            result[key] = {"min": mn, "max": mx, "neutral": neutral}

    return result


def build_app(bridge: TelemetryBridge, description_dir: Optional[Path] = None) -> web.Application:
    @web.middleware
    async def no_cache_static(request: web.Request, handler):
        response = await handler(request)
        if request.path == "/" or request.path.startswith("/static/"):
            response.headers["Cache-Control"] = "no-store, no-cache, must-revalidate, max-age=0"
            response.headers["Pragma"] = "no-cache"
            response.headers["Expires"] = "0"
        return response

    app = web.Application(middlewares=[no_cache_static])

    async def on_startup(_: web.Application) -> None:
        await bridge.start()

    async def on_cleanup(_: web.Application) -> None:
        await bridge.stop()

    app.on_startup.append(on_startup)
    app.on_cleanup.append(on_cleanup)

    async def index(_: web.Request) -> web.FileResponse:
        return web.FileResponse(bridge.web_dir / "index.html")

    # Serve joint_mapping.json merged with live servo calibration from DefaultParameters.h
    mapping_path = Path(__file__).parent / "joint_mapping.json"
    servo_cal = parse_servo_calibration()
    if servo_cal:
        LOG.info("Parsed %d servo calibrations from DefaultParameters.h", len(servo_cal))

    async def joint_mapping(_: web.Request) -> web.Response:
        if not mapping_path.is_file():
            return web.Response(status=404, text="joint_mapping.json not found")
        cfg = json.loads(mapping_path.read_text())
        if servo_cal:
            cfg["servo_calibration"] = servo_cal
        return web.json_response(cfg)

    app.router.add_get("/", index)
    app.router.add_get("/ws", bridge.ws_handler)
    app.router.add_get("/static/joint_mapping.json", joint_mapping)
    app.router.add_static("/static", bridge.web_dir)
    if description_dir and description_dir.is_dir():
        app.router.add_static("/description", description_dir)
    return app


async def run_server(args: argparse.Namespace) -> None:
    web_dir = Path(__file__).parent / "web"
    bridge = TelemetryBridge(
        serial_port=args.serial,
        baud=args.baud,
        reconnect_s=args.reconnect,
        web_dir=web_dir,
        simulate=args.simulate,
    )

    description_dir = Path(args.description_dir) if args.description_dir else (
        Path(__file__).resolve().parent.parent.parent / "description"
    )
    app = build_app(bridge, description_dir)
    runner = web.AppRunner(app)
    await runner.setup()
    site = web.TCPSite(runner, args.host, args.port)
    await site.start()

    LOG.info("Telemetry UI running at http://%s:%d", args.host, args.port)
    stop_event = asyncio.Event()
    loop = asyncio.get_running_loop()

    def stop(sig_name: str) -> None:
        if stop_event.is_set():
            LOG.error("Second %s received; forcing exit", sig_name)
            os._exit(130)
            return
        LOG.info("%s received; shutting down", sig_name)
        stop_event.set()

    for sig in (signal.SIGINT, signal.SIGTERM):
        try:
            loop.add_signal_handler(sig, lambda s=sig: stop(signal.Signals(s).name))
        except NotImplementedError:
            pass

    try:
        await stop_event.wait()
        await asyncio.wait_for(runner.cleanup(), timeout=5.0)
    except asyncio.TimeoutError:
        LOG.error("Shutdown cleanup timed out; forcing exit")
        os._exit(130)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Local serial -> websocket telemetry UI")
    parser.add_argument("--serial", default=None, help="Serial device path, e.g. /dev/ttyUSB0")
    parser.add_argument("--baud", type=int, default=115200, help="Serial baud rate")
    parser.add_argument("--host", default="127.0.0.1", help="Bind host")
    parser.add_argument("--port", type=int, default=8765, help="HTTP/WebSocket port")
    parser.add_argument("--reconnect", type=float, default=1.5, help="Reconnect delay seconds")
    parser.add_argument("--simulate", action="store_true", help="Generate synthetic telemetry (no hardware needed)")
    parser.add_argument("--description-dir", default=None, help="Path to URDF description directory")
    parser.add_argument("--log-level", default="INFO", help="Logging level")
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    logging.basicConfig(
        level=getattr(logging, args.log_level.upper(), logging.INFO),
        format="%(asctime)s %(levelname)s %(name)s: %(message)s",
    )
    asyncio.run(run_server(args))


if __name__ == "__main__":
    main()
