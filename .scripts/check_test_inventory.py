#!/usr/bin/env python3
"""Validate that every host test file has a DEPS_ declaration in the Makefile."""

from __future__ import annotations

import re
from pathlib import Path

from gate_lib import GateReporter


MAKEFILE = Path("Makefile")
MAKE_DIR = Path(".make")
TEST_DIR = Path("test")
DEPS_RE = re.compile(r"^DEPS_(test_[A-Za-z0-9_]+)\s*:=", re.MULTILINE)


def main() -> int:
    reporter = GateReporter("check-test-inventory")

    makefile_text = MAKEFILE.read_text(encoding="utf-8")
    for mk in sorted(MAKE_DIR.glob("*.mk")):
        makefile_text += "\n" + mk.read_text(encoding="utf-8")
    declared_deps = set(DEPS_RE.findall(makefile_text))
    test_files = {path.stem for path in TEST_DIR.glob("test_*.cpp")}

    missing_deps = sorted(test_files - declared_deps)
    orphan_deps = sorted(declared_deps - test_files)

    if missing_deps:
        for t in missing_deps:
            reporter.fail("test-inventory", f"test/{t}.cpp", 0,
                          f"test file missing DEPS_ declaration: {t}")
    if orphan_deps:
        for t in orphan_deps:
            reporter.fail("test-inventory", "Makefile", 0,
                          f"DEPS_ declaration without matching test file: {t}")

    if not missing_deps and not orphan_deps:
        reporter.pass_("test-inventory",
                        message=f"Makefile test inventory matches {len(test_files)} host test files")

    return reporter.finish()


if __name__ == "__main__":
    raise SystemExit(main())
