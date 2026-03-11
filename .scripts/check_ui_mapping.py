#!/usr/bin/env python3
"""Validate telemetry UI joint mapping against schema and URDF assets."""

from __future__ import annotations

import json
import re
import sys
import xml.etree.ElementTree as ET
from pathlib import Path


REPO_ROOT = Path(".")
MAPPING_PATH = REPO_ROOT / "tools/telemetry_ui/joint_mapping.json"
DESCRIPTION_ROOT = REPO_ROOT / "description"
SERVO_KEY_RE = re.compile(r"^(body|dome):\d+$")
NUMERIC_KEY_RE = re.compile(r"^\d+$")


def fail(message: str) -> None:
    print(f"FAIL: {message}")
    raise SystemExit(1)


def load_mapping() -> dict:
    try:
        return json.loads(MAPPING_PATH.read_text(encoding="utf-8"))
    except FileNotFoundError:
        fail(f"{MAPPING_PATH} not found")
    except json.JSONDecodeError as exc:
        fail(f"{MAPPING_PATH} is not valid JSON: {exc}")


def normalize_names(value: object, *, allow_empty_list: bool = True) -> list[str]:
    if isinstance(value, str):
        if not value.strip():
            fail("mapping entry must not be an empty string")
        return [value]
    if isinstance(value, list):
        names: list[str] = []
        for item in value:
            if not isinstance(item, str) or not item.strip():
                fail("mapping arrays must contain only non-empty strings")
            names.append(item)
        if not allow_empty_list and not names:
            fail("mapping entry must not be an empty list")
        return names
    fail(f"mapping entry must be a string or list of strings, got {type(value).__name__}")


def resolve_urdf_path(mapping: dict) -> Path:
    urdf_file = mapping.get("urdf_file")
    urdf_package = mapping.get("urdf_package")
    if not isinstance(urdf_file, str) or not urdf_file.strip():
        fail("urdf_file missing or not a string")
    if not isinstance(urdf_package, str) or not urdf_package.strip():
        fail("urdf_package missing or not a string")

    package_candidates = {
        "fusion2urdf_description": DESCRIPTION_ROOT / "fusion2urdf",
        "description": DESCRIPTION_ROOT,
    }
    package_root = package_candidates.get(urdf_package, DESCRIPTION_ROOT / urdf_package)
    urdf_path = package_root / urdf_file
    if not urdf_path.exists():
        fail(f"referenced URDF file not found: {urdf_path}")
    return urdf_path


def collect_urdf_names(urdf_path: Path) -> tuple[set[str], set[str]]:
    try:
        root = ET.fromstring(urdf_path.read_text(encoding="utf-8"))
    except ET.ParseError as exc:
        fail(f"failed to parse URDF {urdf_path}: {exc}")

    joints = {elem.attrib["name"] for elem in root.findall("joint") if "name" in elem.attrib}
    links = {elem.attrib["name"] for elem in root.findall("link") if "name" in elem.attrib}
    return joints, links


def validate_section_keys(section_name: str, section: object, pattern: re.Pattern[str]) -> dict[str, object]:
    if not isinstance(section, dict):
        fail(f"{section_name} missing or not an object")
    entries: dict[str, object] = {}
    for key, value in section.items():
        if key.startswith("_"):
            continue
        if not pattern.match(key):
            fail(f"{section_name} key has invalid format: {key}")
        entries[key] = value
    return entries


def main() -> int:
    mapping = load_mapping()
    urdf_path = resolve_urdf_path(mapping)
    urdf_joints, urdf_links = collect_urdf_names(urdf_path)

    servo_entries = validate_section_keys("servo_joints", mapping.get("servo_joints"), SERVO_KEY_RE)
    dome_entries = validate_section_keys(
        "dome_servo_joints", mapping.get("dome_servo_joints"), SERVO_KEY_RE
    )
    motor_entries = validate_section_keys("motor_joints", mapping.get("motor_joints"), NUMERIC_KEY_RE)
    speed_entries = validate_section_keys(
        "motor_rad_per_sec", mapping.get("motor_rad_per_sec"), NUMERIC_KEY_RE
    )
    led_entries = validate_section_keys("led_links", mapping.get("led_links"), NUMERIC_KEY_RE)

    for section_name, entries in (
        ("servo_joints", servo_entries),
        ("dome_servo_joints", dome_entries),
    ):
        for key, value in entries.items():
            names = normalize_names(value)
            missing = sorted(name for name in names if name not in urdf_joints)
            if missing:
                fail(f"{section_name}.{key} references joints missing from {urdf_path}: {', '.join(missing)}")

    for key, value in motor_entries.items():
        names = normalize_names(value, allow_empty_list=False)
        if len(names) != 1:
            fail(f"motor_joints.{key} must map to exactly one joint")
        if names[0] not in urdf_joints:
            fail(f"motor_joints.{key} references missing joint '{names[0]}' in {urdf_path}")

    for key, value in led_entries.items():
        names = normalize_names(value, allow_empty_list=False)
        if len(names) != 1:
            fail(f"led_links.{key} must map to exactly one link")
        if names[0] not in urdf_links:
            fail(f"led_links.{key} references missing link '{names[0]}' in {urdf_path}")

    if set(speed_entries) != set(motor_entries):
        missing_speed = sorted(set(motor_entries) - set(speed_entries))
        extra_speed = sorted(set(speed_entries) - set(motor_entries))
        details: list[str] = []
        if missing_speed:
            details.append("missing speeds for motors: " + ", ".join(missing_speed))
        if extra_speed:
            details.append("speeds declared for unknown motors: " + ", ".join(extra_speed))
        fail("; ".join(details))

    for key, value in speed_entries.items():
        if not isinstance(value, (int, float)) or value <= 0:
            fail(f"motor_rad_per_sec.{key} must be a positive number")

    up_axis = mapping.get("urdf_up_axis")
    if up_axis not in {"X", "Y", "Z"}:
        fail("urdf_up_axis must be one of X, Y, Z")

    print(
        "PASS: joint mapping validated "
        f"({len(servo_entries) + len(dome_entries)} servo keys, "
        f"{len(motor_entries)} motors, {len(led_entries)} leds)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
