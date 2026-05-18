#!/usr/bin/env python3
"""Run all agent gate checks and produce a structured JSON report.

Called by: make report-agent-gates
Output: build/agent-gate-report.json + summary to stdout
"""

from __future__ import annotations

import json
import subprocess
import sys
import time
from pathlib import Path

REPORT_PATH = Path("build/agent-gate-report.json")

# Each check: (name, make_target, blocking)
CHECKS = [
    ("environment", "check-environment", True),
    ("test", "test", True),
    ("lint-tidy", "lint-tidy", True),
    ("lint-embedded", "lint-embedded", True),
    ("check-format", "check-format", True),
    ("check-safety", "check-safety", True),
    ("check-docs", "check-docs", True),
    ("check-traceability", "check-traceability", False),
    ("check-ui", "check-ui", False),
    ("check-source-inventory", "check-source-inventory", True),
    ("check-capacity", "check-capacity", False),
    ("check-safety-ordering", "check-safety-ordering", True),
    ("check-safety-paths", "check-safety-paths", True),
]


def run_check(name: str, target: str) -> dict:
    start = time.time()
    proc = subprocess.run(
        ["make", "--no-print-directory", target],
        capture_output=True,
        text=True,
        timeout=600,
    )
    elapsed = round(time.time() - start, 2)
    return {
        "name": name,
        "target": target,
        "passed": proc.returncode == 0,
        "exit_code": proc.returncode,
        "elapsed_seconds": elapsed,
        "output": (proc.stdout + proc.stderr).strip()[-2000:],  # last 2000 chars
    }


def main() -> int:
    results: list[dict] = []
    passed = 0
    failed = 0
    blocking_failures = 0

    print("== Agent Gate Report ==")
    for name, target, blocking in CHECKS:
        try:
            result = run_check(name, target)
        except subprocess.TimeoutExpired:
            result = {
                "name": name,
                "target": target,
                "passed": False,
                "exit_code": -1,
                "elapsed_seconds": 600,
                "output": "TIMEOUT after 600s",
            }
        except FileNotFoundError:
            result = {
                "name": name,
                "target": target,
                "passed": False,
                "exit_code": -1,
                "elapsed_seconds": 0,
                "output": "make not found",
            }

        result["blocking"] = blocking
        results.append(result)

        status = "PASS" if result["passed"] else "FAIL"
        marker = " [BLOCKING]" if not result["passed"] and blocking else ""
        print(f"  {status}: {name} ({result['elapsed_seconds']}s){marker}")

        if result["passed"]:
            passed += 1
        else:
            failed += 1
            if blocking:
                blocking_failures += 1

    report = {
        "timestamp": time.strftime("%Y-%m-%dT%H:%M:%S%z"),
        "total": len(results),
        "passed": passed,
        "failed": failed,
        "blocking_failures": blocking_failures,
        "checks": results,
    }

    REPORT_PATH.parent.mkdir(parents=True, exist_ok=True)
    REPORT_PATH.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")

    print(f"\n== {passed}/{len(results)} checks passed ==")
    if blocking_failures > 0:
        print(f"== {blocking_failures} BLOCKING failure(s) ==")
    print(f"Report: {REPORT_PATH}")

    return 1 if blocking_failures > 0 else 0


if __name__ == "__main__":
    raise SystemExit(main())
