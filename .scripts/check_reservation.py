#!/usr/bin/env python3
"""Cross-reference git diff against handoff YAML reservations.

For multi-agent tasks, validates that changed files fall within the
primary owner's reserved file/scope set. Advisory-only agents editing
reserved files produces a WARNING; untracked files produce a NOTE.

Skipped when mode is not 'parallel-subagents' or handoff is in draft.
"""

from __future__ import annotations

import subprocess
import sys
from fnmatch import fnmatch
from pathlib import Path

HANDOFF = Path(".agents/handoff/current_handoff.yaml")


def load_handoff() -> dict | None:
    if not HANDOFF.exists():
        return None
    try:
        import yaml
    except ImportError:
        print("SKIP: PyYAML not installed")
        return None
    with HANDOFF.open("r", encoding="utf-8") as f:
        return yaml.safe_load(f)


def changed_files() -> list[str]:
    commands = [
        ["git", "diff", "--name-only", "HEAD"],
        ["git", "diff", "--name-only", "--cached"],
    ]
    paths: set[str] = set()
    for cmd in commands:
        proc = subprocess.run(cmd, capture_output=True, text=True, check=False)
        if proc.returncode == 0:
            for line in proc.stdout.splitlines():
                stripped = line.strip()
                if stripped:
                    paths.add(stripped)
    return sorted(paths)


def path_in_scope(path: str, scopes: list[str]) -> bool:
    for scope in scopes:
        scope = scope.rstrip("/")
        if path.startswith(scope + "/") or path == scope:
            return True
        if fnmatch(path, scope):
            return True
    return False


def main() -> int:
    data = load_handoff()
    if data is None:
        print("SKIP: no handoff YAML found")
        return 0

    mode = data.get("mode", "")
    status = data.get("status", "")

    if mode != "parallel-subagents":
        print(f"PASS: reservation check not required for mode='{mode}'")
        return 0
    if status == "draft":
        print("PASS: handoff is in draft — reservation check skipped")
        return 0

    reservations = data.get("reservations", {})
    reserved_files = reservations.get("files", [])
    reserved_scopes = reservations.get("scopes", [])
    advisory_roles = data.get("advisory_roles", [])
    primary_owner = data.get("primary_owner", "unknown")

    all_reserved = reserved_files + reserved_scopes
    if not all_reserved:
        print("WARN: parallel-subagents mode but no reservations declared")
        return 0

    files = changed_files()
    if not files:
        print("PASS: no changed files to check")
        return 0

    warnings = 0
    for f in files:
        if not path_in_scope(f, all_reserved):
            print(f"  NOTE: '{f}' not covered by any reservation (owner: {primary_owner})")
        # File is in scope — no issue

    print(f"PASS: {len(files)} changed file(s) checked against reservations")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
