#!/usr/bin/env python3
"""Semantic safety analysis: verify safety gate precedes actuator dispatch.

Parses bridge node headers to check that within onCommand handlers,
any degradation/safety mode check appears before driver dispatch calls.
This is the canonical implementation -- check_safety.sh delegates here
for ordering analysis.

Comment-aware: skips // line comments and /* block comments */ so that
patterns in comments do not produce false positives.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

from gate_lib import GateReporter

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


def strip_comments(text: str) -> list[tuple[int, str]]:
    """Return (original_line_number, code_only_text) pairs with comments removed.

    Handles:
    - // line comments (removed from that point to EOL)
    - /* block comments */ (removed, even multi-line)
    - String literals (preserves content inside quotes)
    """
    result: list[tuple[int, str]] = []
    lines = text.splitlines()
    in_block_comment = False

    for i, line in enumerate(lines, 1):
        cleaned = []
        j = 0
        while j < len(line):
            if in_block_comment:
                end = line.find("*/", j)
                if end == -1:
                    break  # rest of line is still in block comment
                j = end + 2
                in_block_comment = False
                continue

            ch = line[j]
            # String literal -- skip to closing quote
            if ch == '"':
                end = j + 1
                while end < len(line) and line[end] != '"':
                    if line[end] == '\\':
                        end += 1  # skip escaped char
                    end += 1
                cleaned.append(line[j : end + 1])
                j = end + 1
                continue

            # Line comment
            if ch == '/' and j + 1 < len(line) and line[j + 1] == '/':
                break  # rest of line is comment

            # Block comment start
            if ch == '/' and j + 1 < len(line) and line[j + 1] == '*':
                in_block_comment = True
                j += 2
                continue

            cleaned.append(ch)
            j += 1

        result.append((i, "".join(cleaned)))

    return result


def extract_handler_body(
    lines: list[tuple[int, str]], handler_name: str = "onCommand"
) -> list[tuple[int, str]] | None:
    """Extract lines of a handler method, returning (line_number, line_text) pairs."""
    in_handler = False
    brace_depth = 0
    found_first_brace = False
    result: list[tuple[int, str]] = []

    for line_num, line_text in lines:
        if not in_handler:
            if f"void {handler_name}" in line_text:
                in_handler = True
                brace_depth = 0
                found_first_brace = False
                # Check if opening brace is on the same line
                if "{" in line_text:
                    found_first_brace = True
                    brace_depth += line_text.count("{") - line_text.count("}")
                    result.append((line_num, line_text))
                    if brace_depth <= 0:
                        break
            continue

        if not found_first_brace:
            if "{" in line_text:
                found_first_brace = True
                brace_depth += line_text.count("{") - line_text.count("}")
                result.append((line_num, line_text))
                if brace_depth <= 0:
                    break
            continue

        brace_depth += line_text.count("{") - line_text.count("}")
        result.append((line_num, line_text))
        if brace_depth <= 0:
            break

    return result if result else None


def check_bridge_node(path: Path, reporter: GateReporter) -> None:
    """Check a single bridge node for safety gate ordering."""
    text = path.read_text(encoding="utf-8")
    node_name = path.stem
    rel_path = str(path.relative_to("."))

    # Strip comments before analysis
    clean_lines = strip_comments(text)

    handler = extract_handler_body(clean_lines)
    if handler is None:
        return  # no onCommand handler

    # Find first gate line and first dispatch line
    first_gate_line: int | None = None
    first_dispatch_line: int | None = None

    for line_num, line_text in handler:
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
        return  # no dispatch calls

    if first_gate_line is None:
        reporter.fail(
            node_name, rel_path, first_dispatch_line,
            f"dispatches to driver without safety gate in onCommand handler",
            severity="BLOCKING",
        )
        return

    if first_gate_line > first_dispatch_line:
        reporter.fail(
            node_name, rel_path, first_dispatch_line,
            f"safety gate (line {first_gate_line}) appears AFTER first driver dispatch (line {first_dispatch_line})",
            severity="BLOCKING",
        )
    else:
        reporter.pass_(
            node_name, rel_path, first_gate_line,
            f"safety gate (line {first_gate_line}) precedes driver dispatch (line {first_dispatch_line})",
        )


def main() -> int:
    reporter = GateReporter("check-safety-ordering")

    if not BRIDGE_DIR.is_dir():
        reporter.skip("bridge node directory not found")
        return reporter.finish()

    for header in sorted(BRIDGE_DIR.glob("*BridgeNode.h")):
        check_bridge_node(header, reporter)

    return reporter.finish()


if __name__ == "__main__":
    raise SystemExit(main())
