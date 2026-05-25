#!/usr/bin/env python3
"""
Integration test for TelemetryParser: feeds sample TEL: lines and validates output.
Called by: make test-ui-integration
"""
import sys
import os
import json

# Add telemetry_ui to path so we can import TelemetryParser
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools", "telemetry_ui"))
from app import TelemetryParser

test_count = 0
pass_count = 0
fail_count = 0


def test(name):
    global test_count
    test_count += 1
    print(f"TEST: {name} ... ", end="")


def passed():
    global pass_count
    pass_count += 1
    print("PASS")


def failed(reason):
    global fail_count
    fail_count += 1
    print(f"FAIL: {reason}")


parser = TelemetryParser()

# --- JSON format ---
test("json_telemetry_basic")
result = parser.parse_line('TEL:{"inputs":{"drive":{"connected":1,"buttons":0,"axes":[0,0,0,0]}},"outputs":{"motors":[{"id":0,"value":0.5}],"servos":[{"id":1,"value":1500,"group":"body"}]}}')
basic_result = result
if result is None:
    failed("parse returned None")
elif result.get("kind") != "telemetry":
    failed(f"kind={result.get('kind')}, expected telemetry")
elif not isinstance(result.get("controllers"), list):
    failed("missing controllers list")
elif not isinstance(result.get("motors"), list):
    failed("missing motors list")
elif not isinstance(result.get("servos"), list):
    failed("missing servos list")
else:
    passed()

test("json_motor_values")
if result and result.get("motors"):
    m = result["motors"][0]
    if m.get("id") == 0 and m.get("value") == 0.5:
        passed()
    else:
        failed(f"motor={m}, expected id=0 value=0.5")
else:
    failed("no motors in result")

test("json_servo_values")
if result and result.get("servos"):
    s = result["servos"][0]
    if s.get("id") == 1 and s.get("value") == 1500:
        passed()
    else:
        failed(f"servo={s}, expected id=1 value=1500")
else:
    failed("no servos in result")

test("json_servo_command_name")
result = parser.parse_line('TEL:{"outputs":{"servos":[{"id":1,"type":1,"value":20,"group":"dome"}]}}')
if result and result.get("servos"):
    s = result["servos"][0]
    if s.get("type") == 1 and s.get("command") == "speed":
        passed()
    else:
        failed(f"servo={s}, expected type=1 command=speed")
else:
    failed("no servos in result")

test("json_controller_connected")
if basic_result and basic_result.get("controllers"):
    drive_ctrl = next((c for c in basic_result["controllers"] if c.get("role") == "drive"), None)
    if drive_ctrl and drive_ctrl.get("connected"):
        passed()
    else:
        failed(f"drive controller not connected: {drive_ctrl}")
else:
    failed("no controllers in result")

# --- Non-TEL line ---
test("non_tel_line_returns_none")
result = parser.parse_line("INFO: Boot complete")
if result is None:
    passed()
else:
    failed(f"expected None, got {result}")

# --- Empty TEL ---
test("empty_tel_returns_none")
result = parser.parse_line("TEL:")
if result is None:
    passed()
else:
    failed(f"expected None, got {result}")

# --- Malformed JSON ---
test("malformed_json_returns_parse_error")
result = parser.parse_line("TEL:{bad json")
if result and result.get("kind") == "parse_error":
    passed()
else:
    failed(f"expected parse_error, got {result}")

# --- Compact format (key=value) ---
test("compact_format_parses")
result = parser.parse_line("TEL:d_conn=1 d_lx=100 d_ly=-50")
if result is not None and result.get("kind") == "telemetry":
    passed()
else:
    failed(f"expected telemetry, got {result}")

test("firmware_compact_format_parses")
result = parser.parse_line(
    "TEL:t=123456 mode=0 estop=0/0 "
    "drv=1 d_btn=0x00c0 d_edge=0x00c0 d_misc=0x08 d_medge=0x08 d_dpad=0x00 "
    "d_ax=(0,0,-409,0) "
    "dome=1 m_btn=0x0001 m_edge=0x0001 m_misc=0x00 m_medge=0x00 m_dpad=0x00 "
    "m_ax=(0,0,0,0) "
    "m0=0.1234 m1=-0.1234 m2=0.5000 sb3=1500.0 sd1=1282.0 sound_type=0 track=254"
)
if result is None:
    failed("parse returned None")
else:
    drive_ctrl = next((c for c in result["controllers"] if c.get("role") == "drive"), None)
    dome_ctrl = next((c for c in result["controllers"] if c.get("role") == "dome"), None)
    if result.get("format") != "compact":
        failed(f"format={result.get('format')}, expected compact")
    elif not drive_ctrl or not drive_ctrl.get("connected") or drive_ctrl.get("buttons") != 0x00C0:
        failed(f"bad drive controller: {drive_ctrl}")
    elif not dome_ctrl or not dome_ctrl.get("connected") or dome_ctrl.get("buttons") != 0x0001:
        failed(f"bad dome controller: {dome_ctrl}")
    elif len(result.get("motors", [])) != 3:
        failed(f"motors={result.get('motors')}, expected 3")
    elif not any(s.get("group") == "body" and s.get("id") == 3 for s in result.get("servos", [])):
        failed(f"missing body servo: {result.get('servos')}")
    elif not any(s.get("group") == "dome" and s.get("id") == 1 for s in result.get("servos", [])):
        failed(f"missing dome servo: {result.get('servos')}")
    elif not result.get("audio") or result["audio"].get("type") != 0 or result["audio"].get("track") != 254:
        failed(f"bad compact audio: {result.get('audio')}")
    else:
        passed()

# --- Schema validation ---
test("json_output_has_required_fields")
result = parser.parse_line('TEL:{"inputs":{},"outputs":{}}')
if result is None:
    failed("parse returned None")
else:
    required = ["kind", "controllers", "motors", "servos"]
    missing = [f for f in required if f not in result]
    if missing:
        failed(f"missing fields: {missing}")
    else:
        passed()

# --- Results ---
print(f"\n=== Results: {pass_count}/{test_count} passed ===")
sys.exit(0 if pass_count == test_count else 1)
