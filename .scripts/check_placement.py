#!/usr/bin/env python3
"""Detect logic placed in the wrong pipeline stage.

Checks for known anti-patterns from placement-reasoning.md:
1. Action nodes referencing raw button bitmasks (should be in intent mapping)
2. Bridge nodes containing decision logic beyond safety gating
3. Intent mapping files containing actuator-level logic
4. Action nodes importing BluepadInputNode types (coupling to input layer)

This is heuristic — it catches the common anti-patterns, not all placement
errors. Manual trace via docs/registry.md is still needed for novel cases.
"""

from __future__ import annotations

import re
from pathlib import Path

from gate_lib import GateReporter

NODE_DIR = Path("main/include/chopper/nodes")
INPUT_DIR = Path("main/include/chopper/input")
FIRMWARE_DIR = Path("main/include/chopper")

# Action nodes: everything in nodes/ that is NOT a bridge, tap, or input node
BRIDGE_PATTERN = re.compile(r"BridgeNode|TelemetryIOTapNode|BluepadInputNode|DriverUpdateNode")

# Anti-pattern 1: Raw button constants in action nodes
# Button bitmask defines from Bluepad32 or direct gamepad bit checks
BUTTON_BITMASK_RE = re.compile(
    r"\bBUTTON_[A-Z_]+\b"
    r"|\bgamepad->buttons\b"
    r"|\bpad->buttons\b"
    r"|\bbuttons\s*&\s*0x"
    r"|\b0x[0-9a-fA-F]+\s*&\s*buttons\b"
    r"|\.buttons\(\)"
)

# Anti-pattern 2: Decision logic in bridge nodes
# Bridge nodes should only check degradation mode and forward
DECISION_LOGIC_RE = re.compile(
    r"\bif\s*\("
    r"|\bswitch\s*\("
    r"|\b\?\s*"  # ternary
)
# These are acceptable conditions in bridge nodes (safety gating)
BRIDGE_ALLOWED_RE = re.compile(
    r"degradation_"
    r"|getMode\s*\("
    r"|getAllowMotors\s*\("
    r"|getAllowServos\s*\("
    r"|safety_mode"
    r"|SafetyManager"
    r"|DegradationManager"
    r"|DegradationLevel"
    r"|nullptr"
    r"|!\s*driver_"
    r"|!\s*controller_"
    r"|initialized_"
    r"|\.has_value"
    r"|command_type"       # dispatch by command subtype is forwarding
    r"|CommandType"        # enum switch for command dispatch
    r"|getChannelCount"    # bounds validation before forwarding
    r"|motor_id"           # ID filtering is routing, not decision logic
    r"|servo_id"           # ID filtering is routing, not decision logic
)

# Anti-pattern 3: Actuator-level logic in intent mapping
ACTUATOR_LOGIC_RE = re.compile(
    r"\bdriver_\b"
    r"|\bcontroller_\b"
    r"|->setSpeed\b"
    r"|->setPosition\b"
    r"|->setPulse\b"
    r"|->sendCommand\b"
    r"|->setTarget\b"
)

# Anti-pattern 4: Action nodes importing input-layer types
INPUT_IMPORT_RE = re.compile(
    r'#include.*BluepadInputNode'
    r'|#include.*bluepad32'
    r'|#include.*GamepadPtr'
)


def strip_comments(text: str) -> str:
    """Remove C/C++ comments from source text."""
    # Remove block comments
    text = re.sub(r'/\*.*?\*/', '', text, flags=re.DOTALL)
    # Remove line comments
    text = re.sub(r'//.*$', '', text, flags=re.MULTILINE)
    return text


def is_action_node(path: Path) -> bool:
    """True if this is an action node (not bridge/tap/input)."""
    return not BRIDGE_PATTERN.search(path.stem)


def check_button_bitmasks_in_action_nodes(reporter: GateReporter) -> None:
    """Anti-pattern 1: raw button references in action nodes."""
    if not NODE_DIR.is_dir():
        return

    for header in sorted(NODE_DIR.glob("*.h")):
        if not is_action_node(header):
            continue
        text = strip_comments(header.read_text(encoding="utf-8"))
        rel = str(header.relative_to("."))
        for i, line in enumerate(text.splitlines(), 1):
            if BUTTON_BITMASK_RE.search(line):
                reporter.warn(
                    header.stem, rel, i,
                    f"raw button reference in action node (should be in intent mapping): {line.strip()}",
                )


def check_bridge_decision_logic(reporter: GateReporter) -> None:
    """Anti-pattern 2: non-gating decision logic in bridge nodes."""
    if not NODE_DIR.is_dir():
        return

    for header in sorted(NODE_DIR.glob("*BridgeNode.h")):
        text = strip_comments(header.read_text(encoding="utf-8"))
        lines = text.splitlines()
        rel = str(header.relative_to("."))

        # Find onCommand handlers
        in_handler = False
        brace_depth = 0

        for i, line in enumerate(lines, 1):
            if "void onCommand" in line or "void on_command" in line:
                in_handler = True
                brace_depth = 0
                continue

            if in_handler:
                brace_depth += line.count("{") - line.count("}")
                if brace_depth < 0:
                    in_handler = False
                    continue

                if DECISION_LOGIC_RE.search(line) and not BRIDGE_ALLOWED_RE.search(line):
                    reporter.warn(
                        header.stem, rel, i,
                        f"decision logic in bridge node (bridge should only gate + forward): {line.strip()}",
                    )


def check_actuator_logic_in_mapping(reporter: GateReporter) -> None:
    """Anti-pattern 3: actuator-level calls in intent mapping files."""
    if not INPUT_DIR.is_dir():
        return

    for header in sorted(INPUT_DIR.glob("*Mapping*.h")):
        text = strip_comments(header.read_text(encoding="utf-8"))
        rel = str(header.relative_to("."))
        for i, line in enumerate(text.splitlines(), 1):
            if ACTUATOR_LOGIC_RE.search(line):
                reporter.warn(
                    header.stem, rel, i,
                    f"actuator logic in intent mapping (should be in action/bridge node): {line.strip()}",
                )


def check_input_imports_in_action_nodes(reporter: GateReporter) -> None:
    """Anti-pattern 4: action nodes importing input-layer types."""
    if not NODE_DIR.is_dir():
        return

    for header in sorted(NODE_DIR.glob("*.h")):
        if not is_action_node(header):
            continue
        text = header.read_text(encoding="utf-8")
        rel = str(header.relative_to("."))
        for i, line in enumerate(text.splitlines(), 1):
            if INPUT_IMPORT_RE.search(line):
                reporter.fail(
                    header.stem, rel, i,
                    f"action node imports input-layer type (coupling violation): {line.strip()}",
                )


def main() -> int:
    reporter = GateReporter("check-placement")

    check_button_bitmasks_in_action_nodes(reporter)
    check_bridge_decision_logic(reporter)
    check_actuator_logic_in_mapping(reporter)
    check_input_imports_in_action_nodes(reporter)

    if not reporter.findings:
        reporter.pass_("all", message="No placement anti-patterns detected")

    return reporter.finish()


if __name__ == "__main__":
    raise SystemExit(main())
