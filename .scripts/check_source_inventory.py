#!/usr/bin/env python3
"""Validate that every source .cpp file under main/chopper/ is referenced in the Makefile,
and every .cpp reference in the Makefile actually exists on disk."""

from __future__ import annotations

import re
from pathlib import Path

from gate_lib import GateReporter


MAKEFILE = Path("Makefile")
MAKE_DIR = Path(".make")
SOURCE_DIR = Path("main/chopper")

# Match any main/chopper/.../*.cpp path in Makefile variable definitions or recipes
SOURCE_RE = re.compile(r"(main/chopper/[A-Za-z0-9_/]+\.cpp)")

# ESP32-only files and directories used in the firmware build but not referenced
# in host-test Makefile variables. Exclude from "missing from Makefile" check.
ESP32_ONLY = {
    Path("main/chopper/hal/ChopperBluetooth.cpp"),
    Path("main/chopper/RuntimeMain.cpp"),
    Path("main/chopper/ValidationMain.cpp"),
    Path("main/chopper/adapters/Bluepad32ControllerSource.cpp"),
    Path("main/chopper/examples/ControllerInputNode.cpp"),
}


def main() -> int:
    reporter = GateReporter("check-source-inventory")

    makefile_text = MAKEFILE.read_text(encoding="utf-8")
    for mk in sorted(MAKE_DIR.glob("*.mk")):
        makefile_text += "\n" + mk.read_text(encoding="utf-8")
    makefile_sources = set(Path(p) for p in SOURCE_RE.findall(makefile_text))

    disk_sources = set(SOURCE_DIR.rglob("*.cpp"))

    missing_from_makefile = sorted(disk_sources - makefile_sources - ESP32_ONLY)
    missing_from_disk = sorted(makefile_sources - disk_sources)

    if missing_from_makefile:
        for p in missing_from_makefile:
            reporter.fail("source-inventory", str(p), 0,
                          f"source file missing from Makefile: {p}")
    if missing_from_disk:
        for p in missing_from_disk:
            reporter.fail("source-inventory", "Makefile", 0,
                          f"Makefile references source file that does not exist: {p}")

    if not missing_from_makefile and not missing_from_disk:
        total = len(disk_sources)
        excluded = len(ESP32_ONLY & disk_sources)
        reporter.pass_("source-inventory",
                        message=f"Makefile source inventory matches {total - excluded} host source files ({excluded} ESP32-only excluded)")

    return reporter.finish()


if __name__ == "__main__":
    raise SystemExit(main())
