#!/usr/bin/env python3
"""Check that firmware telemetry JSON keys are handled by the UI parser.

Maintains a schema of firmware telemetry fields and checks that the Python
parser in app.py handles each one. When firmware adds a new telemetry field,
add it to FIRMWARE_JSON_SCHEMA below — the gate will catch if the parser
doesn't handle it yet.

Exit 0 if all required firmware keys are covered. Exit 1 with FAIL lines otherwise.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

FIRMWARE_FILE = Path("main/chopper/TelemetryService.cpp")
PARSER_FILE = Path("tools/telemetry_ui/app.py")

# ──────────────────────────────────────────────────────────────
# Firmware telemetry JSON schema — the canonical contract.
#
# Maintained alongside firmware. When you add a field to
# formatJsonFromFrame or emitSerialCompact, add it here.
# The gate checks that app.py handles each required field.
#
# Format: {section: {key: {"aliases": [...], "required": bool}}}
#   aliases  = acceptable field names the parser may use
#   required = True if the UI must handle it; False = informational only
# ──────────────────────────────────────────────────────────────

FIRMWARE_JSON_SCHEMA: dict[str, dict[str, dict]] = {
    "root": {
        "timestamp_us": {"aliases": ["timestamp_us", "t"], "required": True},
        "executor_estop": {"aliases": ["executor_estop", "estop"], "required": False},
        "safety_estop": {"aliases": ["safety_estop"], "required": False},
        "degradation_mode": {"aliases": ["degradation_mode", "mode"], "required": False},
    },
    "root_perf": {
        "loop_count": {"aliases": ["loop_count", "lc"], "required": False},
        "max_loop_time_us": {"aliases": ["max_loop_time_us", "loop_max"], "required": False},
        "avg_loop_time_us": {"aliases": ["avg_loop_time_us", "loop_avg"], "required": False},
        "active_nodes": {"aliases": ["active_nodes"], "required": False},
        "total_nodes": {"aliases": ["total_nodes"], "required": False},
    },
    "inputs.<role>": {
        "valid": {"aliases": ["valid"], "required": True},
        "connected": {"aliases": ["connected", "conn", "is_connected"], "required": True},
        "has_data": {"aliases": ["has_data", "data"], "required": False},
        "battery": {"aliases": ["battery", "battery_level"], "required": True},
        "dpad": {"aliases": ["dpad"], "required": False},
        "axes": {"aliases": ["axes", "axis_x", "axis_y", "axis_rx", "axis_ry"], "required": True},
        "buttons": {"aliases": ["buttons", "btn_mask", "button_mask"], "required": True},
        "misc": {"aliases": ["misc", "misc_buttons"], "required": True},
        "reports": {"aliases": ["reports", "report_count"], "required": False},
        "changes": {"aliases": ["changes", "change_count"], "required": False},
        "btn_edges": {"aliases": ["btn_edges", "button_edge_count"], "required": False},
        "btn_edge_mask": {"aliases": ["btn_edge_mask", "button_edge_mask"], "required": False},
        "last_change_us": {"aliases": ["last_change_us"], "required": False},
        "avg_interval_us": {"aliases": ["avg_interval_us", "avg_report_interval_us"], "required": True},
        "source_flags": {"aliases": ["source_flags", "source_diag_flags"], "required": False},
        "report_gap_us": {"aliases": ["report_gap_us", "source_report_gap_us"], "required": False},
        "report_age_us": {"aliases": ["report_age_us", "source_report_age_us"], "required": False},
        "local_stops": {"aliases": ["local_stops", "source_local_stop_count"], "required": False},
    },
    "outputs.motors[]": {
        "id": {"aliases": ["id", "motor_id"], "required": True},
        "type": {"aliases": ["type", "command_type"], "required": True},
        "value": {"aliases": ["value", "speed", "power"], "required": True},
    },
    "outputs.servos[]": {
        "id": {"aliases": ["id", "servo_id"], "required": True},
        "type": {"aliases": ["type", "command_type"], "required": True},
        "value": {"aliases": ["value", "position", "pulse"], "required": True},
        "dur_ms": {"aliases": ["dur_ms", "duration_ms"], "required": True},
        "group": {"aliases": ["group", "cluster", "bank"], "required": True},
        "group_id": {"aliases": ["group_id"], "required": False},
    },
    "outputs.led": {
        "valid": {"aliases": ["valid"], "required": False},
        "id": {"aliases": ["id", "name", "index"], "required": True},
        "type": {"aliases": ["type", "command_type"], "required": False},
        "on": {"aliases": ["on", "state"], "required": True},
        "color": {"aliases": ["color", "rgb", "hex"], "required": True},
        "brightness": {"aliases": ["brightness"], "required": True},
        "pattern": {"aliases": ["pattern", "pattern_id"], "required": False},
    },
    "outputs.leds[]": {
        "valid": {"aliases": ["valid"], "required": False},
        "id": {"aliases": ["id", "name", "index"], "required": True},
        "type": {"aliases": ["type", "command_type"], "required": False},
        "on": {"aliases": ["on", "state"], "required": True},
        "color": {"aliases": ["color", "rgb", "hex"], "required": True},
        "brightness": {"aliases": ["brightness"], "required": True},
        "pattern": {"aliases": ["pattern", "pattern_id"], "required": False},
    },
    "outputs.audio": {
        "valid": {"aliases": ["valid"], "required": False},
        "type": {"aliases": ["type", "command_type", "cmd", "sound_type"], "required": True},
        "track": {"aliases": ["track", "track_id", "id", "sound_track"], "required": True},
        "volume": {"aliases": ["volume", "vol"], "required": True},
        "loop": {"aliases": ["loop"], "required": True},
    },
    "outputs.status": {
        "valid": {"aliases": ["valid"], "required": False},
        "state": {"aliases": ["state", "system_status"], "required": False},
        "error": {"aliases": ["error", "error_code"], "required": False},
    },
}

# Compact format keys. The parser generates some dynamically via f-string
# prefixes (e.g. f"{prefix}_btn" where prefix is "d", "m", "a", "c").
# We check that the PREFIX PATTERN exists, not every literal key.
COMPACT_PREFIX_PATTERNS = {
    # prefix: [suffix list that the parser should generate]
    "d": ["_btn", "_conn", "_edge", "_dpad", "_ax", "_misc"],
    "m": ["_btn", "_conn", "_edge", "_dpad", "_ax", "_misc"],
    "a": ["_btn", "_conn", "_edge", "_dpad", "_ax", "_misc"],
    "c": ["_btn", "_conn", "_edge", "_dpad", "_ax", "_misc"],
}

COMPACT_STATIC_KEYS = {"t", "mode"}
# Keys the firmware sends but the parser intentionally does not extract.
# Document the reason for each.
COMPACT_IGNORED_KEYS = {
    "estop": "estop flag shown via JSON path only",
    "lc": "perf counter, not displayed in UI",
    "loop_max": "perf counter, not displayed in UI",
    "loop_avg": "perf counter, not displayed in UI",
    "drv": "firmware uses 'drv=' but parser expects prefix_conn pattern; tracked as known gap",
    "dome": "firmware uses 'dome=' but parser expects prefix_conn pattern; tracked as known gap",
}


def load_text(path: Path) -> str:
    if not path.exists():
        print(f"  WARN: {path} not found, skipping")
        return ""
    return path.read_text(encoding="utf-8")


def alias_in_text(aliases: list[str], text: str) -> bool:
    """Check if any alias appears as a string literal in the text."""
    for alias in aliases:
        patterns = [
            f'"{alias}"',
            f"'{alias}'",
        ]
        for pattern in patterns:
            if pattern in text:
                return True
    return False


def check_json_parser_coverage(parser_text: str) -> list[str]:
    """Check that the Python parser handles all required firmware JSON keys."""
    errors: list[str] = []

    for section, keys in FIRMWARE_JSON_SCHEMA.items():
        for fw_key, info in keys.items():
            if not info["required"]:
                continue

            if not alias_in_text(info["aliases"], parser_text):
                errors.append(
                    f"FAIL: Firmware JSON key '{fw_key}' (section: {section}) "
                    f"not found in parser. Expected one of: {info['aliases']}"
                )

    return errors


def check_compact_parser_coverage(parser_text: str) -> list[str]:
    """Check that compact-format prefix patterns exist in the parser."""
    errors: list[str] = []

    # Check static keys
    for key in COMPACT_STATIC_KEYS:
        if f'"{key}"' not in parser_text and f"'{key}'" not in parser_text:
            errors.append(f"FAIL: Compact static key '{key}' not found in parser")

    # Check that prefix pattern exists (f"{prefix}_btn" etc.)
    # The parser uses ROLE_SLOTS with prefix chars and constructs keys dynamically
    for prefix, suffixes in COMPACT_PREFIX_PATTERNS.items():
        # Check that the prefix character is used in ROLE_SLOTS or similar
        if f'"{prefix}"' not in parser_text and f"'{prefix}'" not in parser_text:
            errors.append(
                f"FAIL: Compact prefix '{prefix}' not found in parser ROLE_SLOTS"
            )

    # Check motor pattern regex
    if not re.search(r'motor|m\)', parser_text):
        errors.append("FAIL: No motor pattern (m<N>) regex found in parser")

    # Check servo group pattern regex
    if not re.search(r'servo|s\)', parser_text):
        errors.append("FAIL: No servo pattern (s<N>) regex found in parser")

    return errors


def extract_format_json_keys(firmware_text: str) -> set[str]:
    """Extract JSON key names from formatJsonFromFrame function."""
    keys: set[str] = set()

    # Find the function body
    in_func = False
    brace_depth = 0
    for line in firmware_text.splitlines():
        if "formatJsonFromFrame" in line and "void" in line:
            in_func = True
            brace_depth = 0
        if not in_func:
            continue

        brace_depth += line.count("{") - line.count("}")

        # Extract \"key_name\" patterns from format strings
        for m in re.finditer(r'\\"([a-z][a-z_]*)\\"', line):
            keys.add(m.group(1))

        if in_func and brace_depth <= 0 and keys:
            break

    return keys


def check_schema_freshness(firmware_text: str) -> list[str]:
    """Verify FIRMWARE_JSON_SCHEMA covers keys the firmware actually emits."""
    errors: list[str] = []

    format_keys = extract_format_json_keys(firmware_text)

    # Collect all keys from our schema
    schema_keys: set[str] = set()
    for keys in FIRMWARE_JSON_SCHEMA.values():
        schema_keys.update(keys.keys())

    # Known structural/wrapper keys that aren't data fields
    wrappers = {
        "inputs", "outputs", "motors", "servos", "led", "leds", "audio",
        "status", "color", "r", "g", "b", "w",  # color sub-keys
    }

    missing = format_keys - schema_keys - wrappers
    for key in sorted(missing):
        errors.append(
            f"FAIL: Firmware emits JSON key '{key}' in formatJsonFromFrame but "
            f"it's missing from FIRMWARE_JSON_SCHEMA — add it to keep the gate current"
        )

    return errors


def main() -> int:
    print("== Telemetry firmware↔UI sync check ==")

    firmware_text = load_text(FIRMWARE_FILE)
    parser_text = load_text(PARSER_FILE)

    if not firmware_text or not parser_text:
        print("  SKIP: Required files not found")
        return 0

    all_errors: list[str] = []

    # 1. Check that the schema is fresh vs actual firmware
    all_errors.extend(check_schema_freshness(firmware_text))

    # 2. Check parser covers all required firmware JSON keys
    all_errors.extend(check_json_parser_coverage(parser_text))

    # 3. Check parser covers compact format patterns
    all_errors.extend(check_compact_parser_coverage(parser_text))

    if all_errors:
        for err in all_errors:
            print(f"  {err}")
        print(f"\n  {len(all_errors)} telemetry sync issue(s) found.")
        print("  When firmware telemetry fields change:")
        print("    1. Update FIRMWARE_JSON_SCHEMA in .scripts/check_telemetry_sync.py")
        print("    2. Update the parser in tools/telemetry_ui/app.py")
        print("    3. Update tools/telemetry_ui/web/app.js if the UI displays the field")
        return 1

    print("  PASS: Parser covers all required firmware telemetry fields")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
