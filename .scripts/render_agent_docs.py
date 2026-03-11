#!/usr/bin/env python3
"""Render agent-facing reference docs from agent_manifest.json."""

from __future__ import annotations

import json
from pathlib import Path


MANIFEST = Path(".agents/manifest.json")
TRIGGER_DOC = Path(".agents/generated/trigger-rules.md")
GOLDEN_DOC = Path(".agents/generated/golden-tasks.md")
AGENT_TEAM_DOC = Path(".agents/generated/agent-team.md")


def load_manifest() -> dict:
    with MANIFEST.open("r", encoding="utf-8") as handle:
        return json.load(handle)


def render_trigger_rules(manifest: dict) -> str:
    lines = [
        "# Auto-Trigger Rules (generated)",
        "",
        "> Source: `.agents/manifest.json`. Regenerate with `python3 .scripts/render_agent_docs.py`.",
        "",
        "Before writing to any behavioral file, compare the planned touch set against this table. If a path matches, read the listed agent file first. Skipping a required read is equivalent to skipping `make test`.",
        "",
        "| If you plan to touch... | Read this first |",
        "|-------------------------|-----------------|",
    ]

    for route in manifest["routes"]:
        pattern_list = ", ".join(f"`{pattern}`" for pattern in route["patterns"])
        read_line = f"`{route['read']}`"
        if route.get("notes"):
            read_line += " -- " + " ".join(route["notes"])
        lines.append(f"| {pattern_list} | {read_line} |")

    lines.extend(
        [
            f"| Multiple subsystems or unclear scope | `{manifest['multi_area_rule']['read']}` |",
            "",
            "No trigger is needed for README, `.gitignore`, CI config, or comment/formatting/whitespace/typo-only edits. If an exempt change becomes behavioral later in the task, perform the required reads before the first behavioral edit.",
            "",
            "Use `make plan-triggers FILES=\"path1 path2\"` for a planned touch set and `make check-triggers` for the current diff.",
            "",
        ]
    )
    return "\n".join(lines)


def render_golden_tasks(manifest: dict) -> str:
    lines = [
        "# Golden Tasks (generated)",
        "",
        "> Source: `.agents/manifest.json`. Regenerate with `python3 .scripts/render_agent_docs.py`.",
        "",
        "These are compact, canonical task shapes for AI agents. Treat them as starting patterns, not rigid templates.",
        "",
    ]

    for task in manifest["golden_tasks"]:
        lines.append(f"## {task['name']}")
        lines.append("")
        lines.append("Touch set:")
        for item in task["touch_set"]:
            lines.append(f"- `{item}`")
        lines.append("")
        lines.append("Read first:")
        for item in task["reads"]:
            lines.append(f"- `{item}`")
        lines.append("")
        lines.append("Verify with:")
        for item in task["verification"]:
            lines.append(f"- `{item}`")
        lines.append("")
        lines.append("Done when:")
        for item in task["done_when"]:
            lines.append(f"- {item}")
        lines.append("")

    return "\n".join(lines)


def render_agent_team(manifest: dict) -> str:
    lines = [
        "# Agent Team (generated)",
        "",
        "> Source: `.agents/manifest.json`. Regenerate with `python3 .scripts/render_agent_docs.py`.",
        "",
        "Role-specific instructions live in `.agents/personas/`. Each file adds role-specific context on top of CLAUDE.md.",
        "",
        "| Agent | File | Writes To | Read-Only? | Load When |",
        "|-------|------|-----------|------------|-----------|",
    ]

    for agent in manifest["agents"]:
        read_only = "yes (findings)" if agent.get("read_only") else ""
        writes_to = agent.get("writes_to", "")
        lines.append(
            f"| {agent['name']} | `{agent['file']}` | {writes_to} | {read_only} | {agent['load_when']} |"
        )

    lines.append("")
    lines.append("Shared workflow policy lives in `.agents/policies/working-agreement.md`.")
    lines.append("")
    return "\n".join(lines)


def main() -> int:
    manifest = load_manifest()
    TRIGGER_DOC.write_text(render_trigger_rules(manifest), encoding="utf-8")
    GOLDEN_DOC.write_text(render_golden_tasks(manifest), encoding="utf-8")
    AGENT_TEAM_DOC.write_text(render_agent_team(manifest), encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
