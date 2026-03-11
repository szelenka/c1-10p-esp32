#!/usr/bin/env python3
"""Validate that every host test file has a DEPS_ declaration in the Makefile."""

from __future__ import annotations

import re
from pathlib import Path


MAKEFILE = Path("Makefile")
TEST_DIR = Path("test")
DEPS_RE = re.compile(r"^DEPS_(test_[A-Za-z0-9_]+)\s*:=", re.MULTILINE)


def main() -> int:
    makefile_text = MAKEFILE.read_text(encoding="utf-8")
    declared_deps = set(DEPS_RE.findall(makefile_text))
    test_files = {path.stem for path in TEST_DIR.glob("test_*.cpp")}

    missing_deps = sorted(test_files - declared_deps)
    orphan_deps = sorted(declared_deps - test_files)

    if missing_deps:
        print("FAIL: test files missing DEPS_ declarations:", ", ".join(missing_deps))
    if orphan_deps:
        print("FAIL: DEPS_ declarations without matching test file:", ", ".join(orphan_deps))

    if missing_deps or orphan_deps:
        return 1

    print(f"PASS: Makefile test inventory matches {len(test_files)} host test files")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
