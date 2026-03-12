#!/usr/bin/env python3
"""Flag changes to high-impact files that require user approval.

High-impact files affect multiple subsystems. Changes should be
intentional and acknowledged. This gate flags — not blocks — so agents
can confirm with the user before proceeding.

Exit codes:
  0 = no high-impact files changed, or all acknowledged
  1 = high-impact files changed without acknowledgment
"""

from __future__ import annotations

import subprocess
import sys

from gate_lib import GateReporter

HIGH_IMPACT_FILES = [
    "main/chopper/Application.cpp",
    "main/include/chopper/Application.h",
    "main/include/chopper/messages/CommonMessages.h",
    "main/include/chopper/chopper_limits.h",
]

IMPACT_MAP = {
    "main/chopper/Application.cpp": "Wires all subsystems — changes affect every area",
    "main/include/chopper/Application.h": "Wires all subsystems — changes affect every area",
    "main/include/chopper/messages/CommonMessages.h": "Shared by all nodes — adding types has fan-out across entire codebase",
    "main/include/chopper/chopper_limits.h": "Fixed-size array caps — increasing affects memory layout system-wide",
}

APPROVAL_MARKERS = [
    "user-approved",
    "approved-by-user",
    "high-impact-ack",
]


def get_changed_files() -> list[str]:
    result = subprocess.run(
        ["git", "diff", "--name-only", "HEAD"],
        capture_output=True, text=True,
    )
    staged = subprocess.run(
        ["git", "diff", "--name-only", "--cached"],
        capture_output=True, text=True,
    )
    files = set(result.stdout.strip().splitlines() + staged.stdout.strip().splitlines())
    return sorted(f for f in files if f)


def main() -> int:
    reporter = GateReporter("check-high-impact")

    changed = get_changed_files()
    hit_files = [f for f in changed if f in HIGH_IMPACT_FILES]

    if not hit_files:
        reporter.pass_("high-impact", message="No high-impact files changed")
        return reporter.finish()

    for f in hit_files:
        impact = IMPACT_MAP.get(f, "high-impact file")
        reporter.fail(
            "high-impact", f, 0,
            f"high-impact file changed — confirm with user. {impact}",
            severity="WARNING",
        )

    return reporter.finish()


if __name__ == "__main__":
    raise SystemExit(main())
