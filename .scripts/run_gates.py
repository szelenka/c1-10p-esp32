#!/usr/bin/env python3
"""Run all gate targets and produce a structured JSON summary.

Usage:
  python3 .scripts/run_gates.py                    # run all gates
  python3 .scripts/run_gates.py check-safety test   # run specific gates

Output: human-readable per-gate results + a GATE_SUMMARY JSON line at the end.

The GATE_SUMMARY line is machine-parseable:
  GATE_SUMMARY:{"passed":13,"failed":2,"warnings":1,"failures":["check-safety","check-docs"],...}
"""

from __future__ import annotations

import json
import os
import re
import subprocess
import sys
import time

# All gates in execution order (matches agent-gate-fast)
ALL_GATES = [
    "check-environment",
    "test",
    "lint-tidy",
    "lint-embedded",
    "check-format",
    "check-assertions",
    "check-placement",
    "check-safety",
    "check-safety-ordering",
    "check-safety-paths",
    "check-safety-reachability",
    "check-docs",
    "check-traceability",
    "check-ui",
    "check-source-inventory",
    "check-triggers",
    "check-generated-docs",
]

# Severity by gate name (for gates that don't emit structured output yet)
GATE_SEVERITY: dict[str, str] = {
    "check-safety": "BLOCKING",
    "check-safety-ordering": "BLOCKING",
    "check-safety-paths": "BLOCKING",
    "check-safety-reachability": "BLOCKING",
    "test": "ERROR",
    "lint-tidy": "WARNING",
    "lint-embedded": "ERROR",
    "check-format": "WARNING",
    "check-assertions": "ERROR",
    "check-placement": "ERROR",
    "check-docs": "WARNING",
    "check-ui": "WARNING",
    "check-source-inventory": "ERROR",
    "check-test-inventory": "ERROR",
    "check-triggers": "WARNING",
    "check-generated-docs": "WARNING",
    "check-traceability": "WARNING",
    "check-environment": "ERROR",
}

# Regex to parse structured GATE: lines from scripts that use gate_lib
GATE_LINE_RE = re.compile(r"^GATE:([^:]+):([^:]+):(.*)$")

# Regex to parse legacy PASS/FAIL/WARN lines
LEGACY_RE = re.compile(r"^\s*(PASS|FAIL|WARN|SKIP)(?::?\s*(.*))?$")
LEGACY_PREFIXED_RE = re.compile(r"^\s*(PASS|FAIL|WARN):\s*(.+)$")
LEGACY_FILE_LINE_RE = re.compile(r"(\S+\.\w+):(\d+)")


def parse_gate_output(gate_name: str, output: str) -> list[dict]:
    """Parse structured and legacy output into finding dicts."""
    findings: list[dict] = []

    for line in output.splitlines():
        # Try structured format first
        m = GATE_LINE_RE.match(line)
        if m:
            findings.append({
                "raw": line,
                "structured": True,
            })
            continue

        # Legacy FAIL/WARN with message
        m = LEGACY_PREFIXED_RE.match(line)
        if m:
            status = m.group(1)
            message = m.group(2)
            file_path = ""
            file_line = 0

            # Try to extract file:line from message
            fm = LEGACY_FILE_LINE_RE.search(message)
            if fm:
                file_path = fm.group(1)
                file_line = int(fm.group(2))

            findings.append({
                "status": status,
                "message": message,
                "file": file_path,
                "line": file_line,
                "structured": False,
            })

    return findings


def run_gate(gate_name: str) -> dict:
    """Run a single make target and return result dict."""
    start = time.monotonic()
    result = subprocess.run(
        ["make", "--no-print-directory", gate_name],
        capture_output=True,
        text=True,
        timeout=300,
    )
    elapsed = time.monotonic() - start

    output = result.stdout + result.stderr
    exit_code = result.returncode

    # Determine status from exit code
    if exit_code == 0:
        status = "PASS"
    else:
        status = "FAIL"

    # Parse findings from output
    findings = parse_gate_output(gate_name, output)

    # Extract FAIL lines for summary
    fail_lines = []
    for f in findings:
        if f.get("status") == "FAIL" or (f.get("structured") and ":FAIL:" in f.get("raw", "")):
            if f.get("structured"):
                fail_lines.append(f["raw"])
            else:
                file_loc = f"{f['file']}:{f['line']}" if f.get("file") else ""
                severity = GATE_SEVERITY.get(gate_name, "ERROR")
                msg = f["message"]
                if file_loc:
                    fail_lines.append(f"GATE:{gate_name}:FAIL:{file_loc}:[{severity}]{msg}")
                else:
                    fail_lines.append(f"GATE:{gate_name}:FAIL::[{severity}]{msg}")

    return {
        "gate": gate_name,
        "status": status,
        "exit_code": exit_code,
        "elapsed_s": round(elapsed, 1),
        "severity": GATE_SEVERITY.get(gate_name, "ERROR"),
        "fail_lines": fail_lines,
        "output": output,
    }


def main() -> int:
    # Determine which gates to run
    if len(sys.argv) > 1:
        gates = sys.argv[1:]
        # Validate
        for g in gates:
            if g not in ALL_GATES:
                print(f"Unknown gate: {g}", file=sys.stderr)
                print(f"Available: {', '.join(ALL_GATES)}", file=sys.stderr)
                return 2
    else:
        gates = ALL_GATES

    results: list[dict] = []
    total_start = time.monotonic()

    print(f"Running {len(gates)} gate(s)...\n")

    for gate in gates:
        sys.stdout.write(f"  {gate} ... ")
        sys.stdout.flush()

        try:
            result = run_gate(gate)
        except subprocess.TimeoutExpired:
            result = {
                "gate": gate,
                "status": "FAIL",
                "exit_code": -1,
                "elapsed_s": 300.0,
                "severity": GATE_SEVERITY.get(gate, "ERROR"),
                "fail_lines": [f"GATE:{gate}:FAIL::timeout after 300s"],
                "output": "",
            }

        results.append(result)

        # Print inline status
        status_icon = {"PASS": "PASS", "FAIL": "FAIL", "WARN": "WARN"}
        print(f"{status_icon.get(result['status'], result['status'])} ({result['elapsed_s']}s)")

        # Print failure details inline
        for line in result["fail_lines"]:
            print(f"    {line}")

    total_elapsed = round(time.monotonic() - total_start, 1)

    # Build summary
    passed = [r for r in results if r["status"] == "PASS"]
    failed = [r for r in results if r["status"] == "FAIL"]
    warned = [r for r in results if r["status"] == "WARN"]

    all_fail_lines = []
    for r in failed:
        all_fail_lines.extend(r["fail_lines"])

    summary = {
        "total": len(results),
        "passed": len(passed),
        "failed": len(failed),
        "warnings": len(warned),
        "elapsed_s": total_elapsed,
        "failures": [r["gate"] for r in failed],
        "failure_details": all_fail_lines,
    }

    # Print human-readable summary
    print(f"\n{'=' * 60}")
    if failed:
        blocking = [r for r in failed if r["severity"] == "BLOCKING"]
        errors = [r for r in failed if r["severity"] == "ERROR"]
        warns = [r for r in failed if r["severity"] == "WARNING"]

        if blocking:
            print(f"BLOCKING: {', '.join(r['gate'] for r in blocking)}")
        if errors:
            print(f"ERROR: {', '.join(r['gate'] for r in errors)}")
        if warns:
            print(f"WARNING: {', '.join(r['gate'] for r in warns)}")

        print(f"\n{len(failed)} failed, {len(passed)} passed ({total_elapsed}s)")
    else:
        print(f"All {len(passed)} gates passed ({total_elapsed}s)")

    # Print machine-parseable summary line
    print(f"\nGATE_SUMMARY:{json.dumps(summary)}")

    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
