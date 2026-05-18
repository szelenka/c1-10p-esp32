"""Shared structured output for gate scripts.

Usage in a gate script:

    from gate_lib import GateReporter

    reporter = GateReporter("check-safety")
    reporter.pass_("MotorBridgeNode", "main/include/chopper/nodes/MotorBridgeNode.h", 42,
                    "safety gate precedes dispatch")
    reporter.fail("ServoBridgeNode", "main/include/chopper/nodes/ServoBridgeNode.h", 88,
                   "DegradationManager gate missing", severity="BLOCKING")
    sys.exit(reporter.finish())

Output format (one per finding, parseable by the gate runner):

    GATE:check-safety:PASS:main/include/chopper/nodes/MotorBridgeNode.h:42:safety gate precedes dispatch
    GATE:check-safety:FAIL:main/include/chopper/nodes/ServoBridgeNode.h:88:DegradationManager gate missing
"""

from __future__ import annotations

import json
import sys
from dataclasses import dataclass, field


@dataclass
class Finding:
    gate: str
    status: str  # PASS, FAIL, WARN, SKIP
    file: str = ""
    line: int = 0
    message: str = ""
    severity: str = ""  # BLOCKING, ERROR, WARNING, NOTE (for FAIL/WARN)
    context: str = ""  # e.g. node name, check name

    def structured_line(self) -> str:
        """Machine-parseable single-line output."""
        loc = self.file
        if self.line > 0:
            loc = f"{self.file}:{self.line}"
        severity = f"[{self.severity}]" if self.severity else ""
        parts = [
            "GATE",
            self.gate,
            self.status,
            loc,
            f"{severity}{self.message}" if severity else self.message,
        ]
        return ":".join(parts)


class GateReporter:
    """Collects findings and emits structured output for a single gate."""

    def __init__(self, gate_name: str) -> None:
        self.gate = gate_name
        self.findings: list[Finding] = []

    def pass_(self, context: str, file: str = "", line: int = 0, message: str = "") -> None:
        f = Finding(self.gate, "PASS", file, line, message or context, context=context)
        self.findings.append(f)
        print(f.structured_line())

    def fail(self, context: str, file: str = "", line: int = 0,
             message: str = "", severity: str = "ERROR") -> None:
        f = Finding(self.gate, "FAIL", file, line, message or context,
                    severity=severity, context=context)
        self.findings.append(f)
        print(f.structured_line())

    def warn(self, context: str, file: str = "", line: int = 0,
             message: str = "", severity: str = "WARNING") -> None:
        f = Finding(self.gate, "WARN", file, line, message or context,
                    severity=severity, context=context)
        self.findings.append(f)
        print(f.structured_line())

    def skip(self, message: str) -> None:
        f = Finding(self.gate, "SKIP", message=message)
        self.findings.append(f)
        print(f.structured_line())

    @property
    def has_failures(self) -> bool:
        return any(f.status == "FAIL" for f in self.findings)

    @property
    def has_warnings(self) -> bool:
        return any(f.status == "WARN" for f in self.findings)

    def summary_dict(self) -> dict:
        return {
            "gate": self.gate,
            "status": "FAIL" if self.has_failures else ("WARN" if self.has_warnings else "PASS"),
            "passed": sum(1 for f in self.findings if f.status == "PASS"),
            "failed": sum(1 for f in self.findings if f.status == "FAIL"),
            "warnings": sum(1 for f in self.findings if f.status == "WARN"),
            "findings": [
                {
                    "status": f.status,
                    "file": f.file,
                    "line": f.line,
                    "message": f.message,
                    "severity": f.severity,
                }
                for f in self.findings
                if f.status in ("FAIL", "WARN")
            ],
        }

    def finish(self) -> int:
        """Print summary and return exit code."""
        return 1 if self.has_failures else 0
