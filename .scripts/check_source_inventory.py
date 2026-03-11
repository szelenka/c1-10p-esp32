#!/usr/bin/env python3
"""Validate that every source .cpp file under main/chopper/ is referenced in the Makefile,
and every .cpp reference in the Makefile actually exists on disk."""

from __future__ import annotations

import re
from pathlib import Path


MAKEFILE = Path("Makefile")
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
    makefile_text = MAKEFILE.read_text(encoding="utf-8")
    makefile_sources = set(Path(p) for p in SOURCE_RE.findall(makefile_text))

    disk_sources = set(SOURCE_DIR.rglob("*.cpp"))

    # Files on disk but not in the Makefile (excluding ESP32-only files)
    missing_from_makefile = sorted(disk_sources - makefile_sources - ESP32_ONLY)
    # Files in the Makefile but not on disk
    missing_from_disk = sorted(makefile_sources - disk_sources)

    if missing_from_makefile:
        print(
            "FAIL: source files missing from Makefile:",
            ", ".join(str(p) for p in missing_from_makefile),
        )
    if missing_from_disk:
        print(
            "FAIL: Makefile references source files that do not exist:",
            ", ".join(str(p) for p in missing_from_disk),
        )

    if missing_from_makefile or missing_from_disk:
        return 1

    total = len(disk_sources)
    excluded = len(ESP32_ONLY & disk_sources)
    print(
        f"PASS: Makefile source inventory matches {total - excluded} host source files"
        f" ({excluded} ESP32-only file(s) excluded)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
