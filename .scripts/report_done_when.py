#!/usr/bin/env python3
"""Report merged done-when checklist for given roles.

Usage:
    python3 .scripts/report_done_when.py [ROLES...]
    python3 .scripts/report_done_when.py implementer tester
    python3 .scripts/report_done_when.py --all
    python3 .scripts/report_done_when.py              # auto-detect from git diff

If no roles are specified, auto-detects from changed files using trigger rules.
"""

import json
import os
import re
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
MANIFEST = REPO / ".agents" / "manifest.json"
PERSONAS_DIR = REPO / ".agents" / "personas"

# Baseline checklist from CLAUDE.md §Final Checklist
BASELINE = [
    "`make test` passes (or failures are in `known_failures.txt`)",
    "`make format` applied (if any code changed)",
    "If safety-relevant: `make check-safety` passes",
    "If docs/registry changed: `make check-docs` passes",
    "No BLOCKING or ERROR findings remain",
]

# Make target associated with each checklist item (for runnable verification)
BASELINE_TARGETS = {
    "`make test`": "test",
    "`make format`": "format",
    "`make check-safety`": "check-safety",
    "`make check-docs`": "check-docs",
}


def load_manifest():
    with open(MANIFEST) as f:
        return json.load(f)


def extract_done_when(persona_path: Path) -> list[str]:
    """Extract checklist items from the Done When section of a persona file."""
    if not persona_path.exists():
        return []
    text = persona_path.read_text()
    # Find the "## Done When" section
    match = re.search(r"## Done When\s*\n(.*?)(?=\n## |\Z)", text, re.DOTALL)
    if not match:
        return []
    section = match.group(1)
    items = []
    for line in section.splitlines():
        # Match checklist items: "- [ ] description"
        m = re.match(r"\s*-\s*\[[ x]\]\s*(.*)", line)
        if m:
            items.append(m.group(1).strip())
    return items


def auto_detect_roles(manifest: dict) -> list[str]:
    """Detect roles from git diff using manifest routes."""
    try:
        result = subprocess.run(
            ["git", "diff", "--name-only", "HEAD"],
            capture_output=True, text=True, cwd=REPO
        )
        changed = result.stdout.strip().splitlines()
        # Also check staged
        result2 = subprocess.run(
            ["git", "diff", "--name-only", "--cached"],
            capture_output=True, text=True, cwd=REPO
        )
        changed.extend(result2.stdout.strip().splitlines())
    except FileNotFoundError:
        return []

    if not changed:
        return []

    from fnmatch import fnmatch
    matched_roles = set()
    for route in manifest.get("routes", []):
        for pattern in route.get("patterns", []):
            for f in changed:
                if fnmatch(f, pattern):
                    # Map area to agent name
                    agent_file = route.get("read", "")
                    for agent in manifest.get("agents", []):
                        if agent.get("file") == agent_file:
                            matched_roles.add(agent["name"])
                            break
    return sorted(matched_roles)


def extract_make_targets(items: list[str]) -> list[str]:
    """Extract runnable make targets from checklist items."""
    targets = []
    for item in items:
        for pattern, target in BASELINE_TARGETS.items():
            if pattern in item:
                targets.append(target)
                break
        # Also check for make targets in role-specific items
        m = re.search(r"`make ([\w-]+)`", item)
        if m:
            t = m.group(1)
            if t not in targets:
                targets.append(t)
    return targets


def main():
    manifest = load_manifest()
    all_agents = {a["name"]: a for a in manifest.get("agents", [])}

    # Determine roles
    if "--all" in sys.argv:
        roles = sorted(all_agents.keys())
    elif len(sys.argv) > 1:
        roles = [r for r in sys.argv[1:] if not r.startswith("-")]
    else:
        roles = auto_detect_roles(manifest)
        if not roles:
            print("No changed files detected. Specify roles or use --all.")
            print(f"Available roles: {', '.join(sorted(all_agents.keys()))}")
            sys.exit(1)

    # Validate roles
    for role in roles:
        if role not in all_agents:
            print(f"Unknown role: {role}")
            print(f"Available: {', '.join(sorted(all_agents.keys()))}")
            sys.exit(1)

    # Collect items
    print("=" * 60)
    print("MERGED DONE-WHEN CHECKLIST")
    print("=" * 60)
    print(f"Roles: {', '.join(roles)}")
    print()

    # Baseline
    print("## Baseline (CLAUDE.md §Final Checklist)")
    for item in BASELINE:
        print(f"  [ ] {item}")
    print()

    # Per-role
    all_role_items = []
    for role in roles:
        agent = all_agents[role]
        persona_path = REPO / agent["file"]
        items = extract_done_when(persona_path)
        if items:
            # Filter out items that just say "CLAUDE.md baseline applies"
            items = [i for i in items if "baseline applies" not in i.lower()]
            if items:
                print(f"## {role} ({agent['file']})")
                for item in items:
                    print(f"  [ ] {item}")
                    all_role_items.append(item)
                print()

    # Runnable verification
    all_items = BASELINE + all_role_items
    targets = extract_make_targets(all_items)
    # Deduplicate while preserving order
    seen = set()
    unique_targets = []
    for t in targets:
        if t not in seen:
            seen.add(t)
            unique_targets.append(t)

    if unique_targets:
        print("## Runnable Verification")
        print(f"  make {' && make '.join(unique_targets)}")
        print()
        print("Or run the full gate:")
        print("  make agent-gate-fast")

    print()
    print("=" * 60)


if __name__ == "__main__":
    main()
