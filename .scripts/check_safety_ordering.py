#!/usr/bin/env python3
"""Semantic safety analysis: verify safety gate precedes actuator dispatch.

Parses bridge node headers to check that within onCommand handlers,
any degradation/safety mode check appears before driver dispatch calls.
This is a deeper check than the grep-based check_safety.sh ordering test.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

BRIDGE_DIR = Path("main/include/chopper/nodes")

# Patterns that indicate a safety gate check
GATE_PATTERNS = [
    re.compile(r"degradation_"),
    re.compile(r"getMode\s*\("),
    re.compile(r"getAllowMotors\s*\("),
    re.compile(r"getAllowServos\s*\("),
    re.compile(r"safety_mode"),
    re.compile(r"SafetyManager"),
    re.compile(r"DegradationManager"),
]

# Patterns that indicate actuator dispatch
DISPATCH_PATTERNS = [
    re.compile(r"driver_->set\b"),
    re.compile(r"driver_->stop\b"),
    re.compile(r"controller_->setPosition\b"),
    re.compile(r"controller_->setSpeed\b"),
    re.compile(r"controller_->enable\b"),
    re.compile(r"controller_->disable\b"),
    re.compile(r"controller_->disableAll\b"),
]


def extract_handler_body(text: str, handler_name: str = "onCommand") -> list[tuple[int, str]] | None:
    """Extract lines of a handler method, returning (line_number, line_text) pairs."""
    lines = text.splitlines()
    in_handler = False
    brace_depth = 0
    result: list[tuple[int, str]] = []

    for i, line in enumerate(lines, 1):
        if not in_handler:
            if f"void {handler_name}" in line:
                in_handler = True
                brace_depth = 0
            continue

        result.append((i, line))
        brace_depth += line.count("{") - line.count("}")
        if brace_depth <= 0 and "{" in text[: text.index(line) + len(line)] if line in text else True:
            # Simpler: track from first { after handler signature
            pass

    # Simpler approach: find handler, collect until balanced braces
    result = []
    in_handler = False
    brace_depth = 0
    found_first_brace = False

    for i, line in enumerate(lines, 1):
        if not in_handler:
            if f"void {handler_name}" in line:
                in_handler = True
                brace_depth = 0
                found_first_brace = False
            continue

        if not found_first_brace:
            if "{" in line:
                found_first_brace = True
                brace_depth += line.count("{") - line.count("}")
                result.append((i, line))
            continue

        brace_depth += line.count("{") - line.count("}")
        result.append((i, line))
        if brace_depth <= 0:
            break

    return result if result else None


def check_bridge_node(path: Path) -> list[str]:
    """Check a single bridge node. Returns list of findings."""
    findings: list[str] = []
    text = path.read_text(encoding="utf-8")
    node_name = path.stem

    handler = extract_handler_body(text)
    if handler is None:
        return findings  # no onCommand handler

    # Find first gate line and first dispatch line
    first_gate_line: int | None = None
    first_dispatch_line: int | None = None

    for line_num, line_text in handler:
        # Skip comments
        stripped = line_text.strip()
        if stripped.startswith("//") or stripped.startswith("*"):
            continue

        if first_gate_line is None:
            for pat in GATE_PATTERNS:
                if pat.search(line_text):
                    first_gate_line = line_num
                    break

        if first_dispatch_line is None:
            for pat in DISPATCH_PATTERNS:
                if pat.search(line_text):
                    first_dispatch_line = line_num
                    break

    if first_dispatch_line is None:
        return findings  # no dispatch calls

    if first_gate_line is None:
        findings.append(
            f"WARN: {node_name}:{first_dispatch_line} dispatches to driver "
            f"without safety gate in onCommand handler"
        )
        return findings

    if first_gate_line > first_dispatch_line:
        findings.append(
            f"ERROR: {node_name} safety gate (line {first_gate_line}) appears "
            f"AFTER first driver dispatch (line {first_dispatch_line})"
        )
    else:
        findings.append(
            f"PASS: {node_name} safety gate (line {first_gate_line}) precedes "
            f"driver dispatch (line {first_dispatch_line})"
        )

    return findings


def main() -> int:
    print("== Semantic safety ordering analysis ==")

    if not BRIDGE_DIR.is_dir():
        print("SKIP: bridge node directory not found")
        return 0

    exit_code = 0
    for header in sorted(BRIDGE_DIR.glob("*BridgeNode.h")):
        for finding in check_bridge_node(header):
            print(f"  {finding}")
            if finding.startswith("ERROR"):
                exit_code = 1

    print("")
    if exit_code == 0:
        print("== Semantic safety analysis passed ==")
    else:
        print("== SEMANTIC SAFETY FAILURES DETECTED ==")
    return exit_code


if __name__ == "__main__":
    raise SystemExit(main())
