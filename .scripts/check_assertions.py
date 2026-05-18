#!/usr/bin/env python3
"""Detect weakened test assertions in staged or uncommitted changes.

Compares removed vs added CHECK/REQUIRE lines in test files.
A net reduction in assertions is suspicious and flagged as FAIL.

Handles:
- CHECK, CHECK_FALSE, CHECK_EQ, CHECK_NE, CHECK_GT, CHECK_LT, etc.
- REQUIRE, REQUIRE_FALSE, REQUIRE_EQ, etc.
- CHECK_THROWS, CHECK_NOTHROW, REQUIRE_THROWS, etc.
- Replacement: removing CHECK and adding REQUIRE (upgrade) is fine.
- Refactoring: changing the condition inside CHECK is fine (net zero).
"""

from __future__ import annotations

import re
import subprocess
import sys

from gate_lib import GateReporter

ASSERTION_RE = re.compile(
    r"\b(CHECK|CHECK_FALSE|CHECK_EQ|CHECK_NE|CHECK_GT|CHECK_LT|CHECK_GE|CHECK_LE"
    r"|CHECK_THROWS|CHECK_THROWS_AS|CHECK_NOTHROW|CHECK_UNARY|CHECK_UNARY_FALSE"
    r"|REQUIRE|REQUIRE_FALSE|REQUIRE_EQ|REQUIRE_NE|REQUIRE_GT|REQUIRE_LT|REQUIRE_GE|REQUIRE_LE"
    r"|REQUIRE_THROWS|REQUIRE_THROWS_AS|REQUIRE_NOTHROW|REQUIRE_UNARY|REQUIRE_UNARY_FALSE"
    r"|ASSERT)\s*\("
)

TEST_FILE_RE = re.compile(r"^test/test_.*\.cpp$")


def get_diff_lines() -> list[str]:
    """Get unified diff of test files (staged + unstaged vs HEAD)."""
    result = subprocess.run(
        ["git", "diff", "HEAD", "--unified=0", "--", "test/test_*.cpp"],
        capture_output=True,
        text=True,
    )
    return result.stdout.splitlines()


def parse_diff(lines: list[str]) -> dict[str, dict[str, int]]:
    """Parse diff output into per-file assertion counts."""
    files: dict[str, dict[str, int]] = {}
    current_file: str | None = None

    for line in lines:
        if line.startswith("+++ b/"):
            path = line[6:]
            if TEST_FILE_RE.match(path):
                current_file = path
                files.setdefault(current_file, {"removed": 0, "added": 0})
            else:
                current_file = None
            continue

        if current_file is None:
            continue

        if line.startswith("-") and not line.startswith("---"):
            content = line[1:]
            if ASSERTION_RE.search(content):
                files[current_file]["removed"] += 1
        elif line.startswith("+") and not line.startswith("+++"):
            content = line[1:]
            if ASSERTION_RE.search(content):
                files[current_file]["added"] += 1

    return files


def get_removed_assertion_lines(lines: list[str]) -> list[tuple[str, str]]:
    """Extract (filename, removed line content) for reporting."""
    results: list[tuple[str, str]] = []
    current_file: str | None = None

    for line in lines:
        if line.startswith("+++ b/"):
            path = line[6:]
            current_file = path if TEST_FILE_RE.match(path) else None
            continue

        if current_file is None:
            continue

        if line.startswith("-") and not line.startswith("---"):
            content = line[1:]
            if ASSERTION_RE.search(content):
                results.append((current_file, content.strip()))

    return results


def main() -> int:
    reporter = GateReporter("check-assertions")

    diff_lines = get_diff_lines()
    if not diff_lines:
        reporter.pass_("assertions", message="No test file changes detected")
        return reporter.finish()

    files = parse_diff(diff_lines)
    if not files:
        reporter.pass_("assertions", message="No test file changes detected")
        return reporter.finish()

    total_removed = 0
    total_added = 0

    for filename, counts in sorted(files.items()):
        removed = counts["removed"]
        added = counts["added"]
        total_removed += removed
        total_added += added

    net_total = total_added - total_removed

    if net_total < 0:
        removed_lines = get_removed_assertion_lines(diff_lines)
        for filename, content in removed_lines:
            reporter.fail(
                "assertion-weakened", filename, 0,
                f"net reduction in assertions (removed {total_removed}, added {total_added}): -{content}",
            )
    else:
        reporter.pass_("assertions",
                        message=f"Assertion integrity OK (removed {total_removed}, added {total_added}, net +{net_total})")

    return reporter.finish()


if __name__ == "__main__":
    raise SystemExit(main())
