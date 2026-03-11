#!/usr/bin/env python3
"""Resolve agent reads from the machine-readable repo manifest."""

from __future__ import annotations

import argparse
import json
from fnmatch import fnmatch
from pathlib import PurePosixPath
from typing import Iterable


REPO_ROOT = PurePosixPath(".")
MANIFEST_PATH = ".agents/manifest.json"


def load_manifest() -> dict:
    with open(MANIFEST_PATH, "r", encoding="utf-8") as handle:
        return json.load(handle)


def normalize(path: str) -> str:
    path = path.strip().replace("\\", "/")
    while path.startswith("./"):
        path = path[2:]
    return path.strip("/")


def path_matches(path: str, pattern: str) -> bool:
    normalized_path = normalize(path)
    normalized_pattern = normalize(pattern)
    if not normalized_pattern:
        return False

    if fnmatch(normalized_path, normalized_pattern):
        return True

    if normalized_pattern.startswith("**/"):
        return fnmatch(normalized_path, normalized_pattern[3:])

    return False


def route_paths(paths: Iterable[str], manifest: dict) -> list[str]:
    normalized_paths = sorted({normalize(path) for path in paths if normalize(path)})
    if not normalized_paths:
        return ["NOTE:No files provided for trigger matching."]

    areas: list[str] = []
    reads: list[str] = []
    notes: list[str] = []

    def add_unique(target: list[str], value: str) -> None:
        if value not in target:
            target.append(value)

    for route in manifest["routes"]:
        matched = any(
            path_matches(path, pattern)
            for path in normalized_paths
            for pattern in route["patterns"]
        )
        if not matched:
            continue

        add_unique(areas, route["area"])
        add_unique(reads, route["read"])
        for note in route.get("notes", []):
            add_unique(notes, note)

    if len(areas) > 1:
        add_unique(reads, manifest["multi_area_rule"]["read"])
        add_unique(notes, manifest["multi_area_rule"]["note"])

    if not reads:
        return ["NOTE:No agent triggers matched for the provided file set."]

    lines: list[str] = []
    lines.extend(f"AREA:{area}" for area in areas)
    lines.extend(f"READ:{read}" for read in reads)
    lines.extend(f"NOTE:{note}" for note in notes)
    return lines


def changed_paths() -> list[str]:
    import subprocess

    commands = [
        ["git", "diff", "--name-only", "HEAD"],
        ["git", "diff", "--name-only", "--cached"],
        ["git", "diff", "--name-only"],
        ["git", "ls-files", "--others", "--exclude-standard"],
    ]

    paths: set[str] = set()
    for cmd in commands:
        proc = subprocess.run(cmd, capture_output=True, text=True, check=False)
        if proc.returncode != 0:
            continue
        for line in proc.stdout.splitlines():
            normalized = normalize(line)
            if normalized:
                paths.add(normalized)
    return sorted(paths)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("paths", nargs="*")
    parser.add_argument("--changed", action="store_true")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    manifest = load_manifest()
    paths = changed_paths() if args.changed else args.paths
    for line in route_paths(paths, manifest):
        print(line)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
