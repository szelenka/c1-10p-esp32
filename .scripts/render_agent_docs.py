#!/usr/bin/env python3
"""Render agent-facing reference docs from manifest.json.

Generates:
  - .agents/generated/trigger-rules.md
  - generated instruction blocks in CLAUDE.md, AGENTS.md, .agents/express.md,
    .cursorrules, .windsurfrules, and .agents/policies/cross-tool.md

Validates:
  - Manifest-referenced files exist
  - Manifest file paths exist where they should
  - Markdown references in key instruction files exist
"""

from __future__ import annotations

import json
import re
import sys
from pathlib import Path


MANIFEST = Path(".agents/manifest.json")
TRIGGER_DOC = Path(".agents/generated/trigger-rules.md")
CLAUDE_MD = Path("CLAUDE.md")
AGENTS_MD = Path("AGENTS.md")
CLAUDE_QUICK_MD = Path(".agents/express.md")
CROSS_TOOL_MD = Path(".agents/policies/cross-tool.md")
CURSOR_RULES = Path(".cursorrules")
WINDSURF_RULES = Path(".windsurfrules")


def command_summary(commands: list[str]) -> str:
    return " + ".join(f"`{command}`" for command in commands)


def minimum_verification_summary(manifest: dict) -> str:
    minimum = manifest["minimum_verification"]
    summary = command_summary(minimum["always"])
    conditional = minimum.get("conditional", [])
    if conditional:
        first = conditional[0]
        summary += f"; if {first['when']}, also run {command_summary(first['commands'])}"
    return summary


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
        role = route.get("role", route["area"])
        read_line = f"`{route['read']}` §{role}"
        if route.get("notes"):
            read_line += " -- " + " ".join(route["notes"])
        lines.append(f"| {pattern_list} | {read_line} |")

    multi_role = manifest["multi_area_rule"].get("role", "orchestrator")
    lines.extend(
        [
            f"| Multiple subsystems or unclear scope | `{manifest['multi_area_rule']['read']}` §{multi_role} |",
            "",
            "No trigger is needed for README, `.gitignore`, CI config, or comment/formatting/whitespace/typo-only edits. If an exempt change becomes behavioral later in the task, perform the required reads before the first behavioral edit.",
            "",
        ]
    )

    # Multi-match rule from route_rules
    route_rules = manifest.get("route_rules")
    if route_rules:
        lines.extend(
            [
                f"**Multi-match rule**: {route_rules['multi_match']}",
                "",
            ]
        )

    lines.extend(
        [
            "Use `make plan-triggers FILES=\"path1 path2\"` for a planned touch set and `make check-triggers` for the current diff.",
            "",
            "## Semantic Triggers (path-based rules cannot catch these)",
            "",
            "Path matching detects *which file* you changed, not *what your change does*. The following situations require safety-critical treatment regardless of which file is edited:",
            "",
            "| Your change... | Trigger | Why paths miss it |",
            "|----------------|---------|-------------------|",
            "| Introduces a new command that reaches a motor or servo (even transitively) | Read `safety-auditor.md`, run `make check-safety` | An action node or intent map change can create a new actuator path without touching `**/safety/**` or `**/nodes/Motor*` |",
            "| Changes `chopper_limits.h` values | Read `architect.md`, run `make check-capacity` | Limit changes affect system-wide resource budgets but don't match subsystem patterns |",
            "| Adds or removes a topic subscriber | Run `make check-capacity` | Subscriber count changes can exceed `MAX_SUBSCRIBERS_PER_TOPIC` without touching safety or HAL files |",
            "| Modifies intent mapping to route a button to an actuator action | Read `safety-auditor.md` | Intent map files live under `main/include/chopper/input/`, not under `**/safety/**` |",
            "| Changes message type fields in `CommonMessages.h` | Flag for all downstream subscribers | Message type changes affect every node that subscribes to topics carrying that type |",
            "| Publishes to a `*/cmd` topic | Run `make check-safety-paths` | New command publishers may bypass bridge node safety gating |",
            "| Changes `TelemetryService.cpp/.h` serialization or adds/changes message fields | Run `make check-telemetry-sync`, update `app.py` parser and `app.js` | Telemetry crosses firmware→parser→UI; path rules only see the file you touched, not the downstream UI consumers |",
            "| Adds a new motor/servo/LED/audio command type or field | Run `make check-telemetry-sync` | New command types are serialized by TelemetryService but the UI parser may not know about the new field |",
            "",
            "**Rule of thumb**: after every change, mentally trace the data flow from your edit to all downstream consumers. If an actuator is reachable, it's safety-critical. If a capacity limit is approached, check headroom. If telemetry format changes, the UI parser must be updated.",
            "",
        ]
    )
    return "\n".join(lines)


def render_semantic_routing(manifest: dict) -> str:
    """Render routing as flattened if-then rules for cross-model transfer."""
    # Map roles to route details (priority, patterns) using explicit role field
    route_details: dict[str, dict] = {}
    for route in manifest.get("routes", []):
        role = route.get("role", route["area"])
        if role not in route_details or route["priority"] < route_details[role].get("priority", 99):
            route_details[role] = {
                "priority": route.get("priority", 99),
                "patterns": route.get("patterns", []),
            }

    # Roles that need .agents/extended.md section references
    deep_refs: dict[str, str] = {
        "safety-auditor": "Safety",
        "orchestrator": "Orchestrator",
        "architect": "Resource Caps",
        "hardware": "Hardware",
    }

    lines = []
    for idx, rule in enumerate(manifest["semantic_routing"], start=1):
        role_name = rule.get("role", Path(rule["load"]).stem)

        details = route_details.get(role_name, {})
        priority = details.get("priority", 99)
        blocking = " **BLOCKING.**" if priority == 0 else ""

        signal = rule["signal"]

        # Add file patterns as hint for mechanical matching
        patterns = details.get("patterns", [])
        pattern_hint = ""
        if patterns and signal != "Everything else":
            short = [f"`{p}`" for p in patterns[:3]]
            pattern_hint = f" (files: {', '.join(short)})"

        # Build the target text: role name + optional .agents/extended.md reference
        deep_section = deep_refs.get(role_name)
        deep_hint = f" Also load [.agents/extended.md](.agents/extended.md) §{deep_section}." if deep_section else ""

        if signal == "Everything else":
            lines.append(f"{idx}. **OTHERWISE** → **{role_name}** role.{blocking}{deep_hint}")
        else:
            lines.append(
                f"{idx}. **IF** {signal.lower()}{pattern_hint} → **{role_name}** role.{blocking}{deep_hint}"
            )

    return "\n".join(lines)


def render_selective_verification(manifest: dict) -> str:
    lines = [
        "| What changed | Run | Skip when |",
        "|---|---|---|",
    ]

    for rule in manifest["verification_rules"]:
        run_text = ", ".join(f"`{command}`" for command in rule["run"])
        if rule.get("note"):
            run_text += f" ({rule['note']})"
        skip_text = rule.get("skip_when") or "\u2014"
        lines.append(f"| {rule['what_changed']} | {run_text} | {skip_text} |")

    return "\n".join(lines)


def render_startup_algorithm(manifest: dict) -> str:
    lines = []
    for idx, item in enumerate(manifest["startup_algorithm"], start=1):
        step = item["step"] if isinstance(item, dict) else item
        lines.append(f"{idx}. {step}")
    return "\n".join(lines)


def render_task_modes(manifest: dict) -> str:
    lines = [
        "| Mode | Scope | Pre-Flight |",
        "|------|-------|------------|",
    ]
    for mode in manifest["task_modes"]:
        preflight = mode["preflight"]
        if mode.get("plan_required"):
            preflight += " Plan required."
        lines.append(f"| {mode['mode']} | {mode['scope']} | {preflight} |")
    return "\n".join(lines)


def render_context_budget(manifest: dict) -> str:
    budget = manifest["context_budget"]
    reads = budget["additional_files_before_first_edit"]
    lines = [
        f"Startup bundle: {', '.join(f'`{item}`' for item in budget['startup_bundle'])}.",
        "",
    ]
    if isinstance(reads, dict):
        lines.extend([
            "Additional files before first edit (mode-dependent):",
            "",
            "| Mode | Max reads before first edit |",
            "|------|---------------------------|",
        ])
        mode_labels = {
            "express": "Express",
            "focused": "Focused / Single-Subsystem",
            "single_subsystem": None,
            "investigate": "Investigate",
            "cross_cutting": "Cross-Cutting / Safety-Critical",
            "safety_critical": None,
        }
        seen_values: set[int] = set()
        for key, label in mode_labels.items():
            if label is None:
                continue
            val = reads.get(key, reads.get("focused", 3))
            if val not in seen_values or key in ("express", "investigate"):
                lines.append(f"| {label} | {val} |")
                seen_values.add(val)
    else:
        lines.append(f"Additional files before first edit: `{reads}`.")
    lines.extend([
        "",
        "Checkpoint triggers:",
    ])
    for trigger in budget["checkpoint_triggers"]:
        lines.append(f"- {trigger}")
    lines.extend(
        [
            "",
            "| Tier | Signal | Behavior |",
            "|------|--------|----------|",
        ]
    )
    for tier in budget["tiers"]:
        lines.append(f"| {tier['name']} | {tier['signal']} | {tier['behavior']} |")
    return "\n".join(lines)


def render_cross_tool_entry_points(manifest: dict) -> str:
    lines = [
        "| Tool | Entry | Notes |",
        "|------|-------|-------|",
    ]
    for item in manifest["cross_tool"]["entry_points"]:
        lines.append(f"| {item['tool']} | `{item['entry']}` | {item['notes']} |")
    return "\n".join(lines)


def render_cross_tool_degradation(manifest: dict) -> str:
    lines = [
        "| If tool can't... | Do this instead |",
        "|---|---|",
    ]
    for item in manifest["cross_tool"]["capability_degradation"]:
        lines.append(f"| {item['if_tool_cant']} | {item['do_this_instead']} |")
    return "\n".join(lines)


def render_persona_capsules(manifest: dict) -> str:
    lines = []
    for capsule in manifest["cross_tool"]["persona_capsules"]:
        lines.append(f"### {capsule['name']}")
        for rule in capsule["lines"]:
            lines.append(f"- {rule}")
        lines.append("")
    return "\n".join(lines).rstrip()


def replace_block(path: Path, marker: str, rendered: str, *, dry_run: bool = False) -> bool:
    """Replace a generated block in a file.

    Returns True if the file is already up-to-date (content matches).
    When dry_run=True, does not write — only checks.
    """
    start_marker = f"<!-- BEGIN GENERATED: {marker} -->"
    end_marker = f"<!-- END GENERATED: {marker} -->"

    text = path.read_text(encoding="utf-8")
    pattern = re.compile(
        rf"{re.escape(start_marker)}.*?{re.escape(end_marker)}",
        re.DOTALL,
    )
    replacement = f"{start_marker}\n{rendered}\n{end_marker}"
    updated, count = pattern.subn(replacement, text, count=1)
    if count != 1:
        raise ValueError(f"{path} is missing generated block {marker}")
    if updated == text:
        return True
    if not dry_run:
        path.write_text(updated, encoding="utf-8")
    return False


def render_entry_points(manifest: dict, *, dry_run: bool = False) -> list[str]:
    """Render all generated blocks. Returns list of stale file:marker pairs."""
    minimum_line = (
        f"Minimum verification: {minimum_verification_summary(manifest)}."
    )

    blocks: list[tuple[Path, str, str]] = [
        (CLAUDE_MD, "startup-algorithm", render_startup_algorithm(manifest)),
        (CLAUDE_MD, "task-modes", render_task_modes(manifest)),
        (CLAUDE_MD, "task-routing", "\n".join([
            "<!-- Generated from .agents/manifest.json. Regenerate with: make render-agent-docs -->",
            "",
            render_semantic_routing(manifest),
        ])),
        (CLAUDE_MD, "verification-rules", render_selective_verification(manifest)),
        (CLAUDE_MD, "context-budget", render_context_budget(manifest)),
        (AGENTS_MD, "minimum-verification", minimum_line),
        (CLAUDE_QUICK_MD, "minimum-verification", minimum_line),
        (CROSS_TOOL_MD, "entry-points", render_cross_tool_entry_points(manifest)),
        (CROSS_TOOL_MD, "minimum-verification", minimum_line),
        (CROSS_TOOL_MD, "capability-degradation", render_cross_tool_degradation(manifest)),
        (CROSS_TOOL_MD, "context-constrained-mode", "\n".join(
            f"{idx}. {item}" for idx, item in enumerate(manifest["cross_tool"]["context_constrained_mode"], start=1)
        )),
        (CROSS_TOOL_MD, "persona-capsules", render_persona_capsules(manifest)),
        (CURSOR_RULES, "minimum-verification", f"# {minimum_line}"),
        (WINDSURF_RULES, "minimum-verification", f"# {minimum_line}"),
    ]

    stale: list[str] = []
    for path, marker, rendered in blocks:
        up_to_date = replace_block(path, marker, rendered, dry_run=dry_run)
        if not up_to_date:
            stale.append(f"{path}:{marker}")
    return stale


def validate_references(manifest: dict) -> list[str]:
    """Validate manifest references and markdown cross-references."""
    errors: list[str] = []

    key_docs = [CLAUDE_MD, AGENTS_MD, CROSS_TOOL_MD]
    if not CLAUDE_MD.exists():
        errors.append("CLAUDE.md not found")
        return errors

    claude_text = CLAUDE_MD.read_text(encoding="utf-8")
    key_texts = {path: path.read_text(encoding="utf-8") for path in key_docs if path.exists()}

    manifest_personas = set()
    for route in manifest["routes"]:
        manifest_personas.add(route["read"])
    manifest_personas.add(manifest["multi_area_rule"]["read"])
    for route in manifest.get("semantic_routing", []):
        manifest_personas.add(route["load"])

    for persona in sorted(manifest_personas):
        if not Path(persona).exists():
            errors.append(f"manifest.json references `{persona}` which does not exist")

    for agent in manifest.get("agents", []):
        agent_file = agent["file"]
        if not Path(agent_file).exists():
            errors.append(
                f"manifest.json agent '{agent['name']}' references `{agent_file}` which does not exist"
            )

    for file_info in manifest.get("high_impact_files", []):
        file_path = file_info["path"]
        if not Path(file_path).exists():
            errors.append(f"manifest.json high-impact file `{file_path}` does not exist")

    checkpoint_template = manifest.get("handoff", {}).get("checkpoint_template")
    if checkpoint_template and not Path(checkpoint_template).exists():
        errors.append(f"manifest.json checkpoint template `{checkpoint_template}` does not exist")

    golden_tasks_file = manifest.get("golden_tasks_file")
    if golden_tasks_file and not Path(golden_tasks_file).exists():
        errors.append(f"manifest.json golden task file `{golden_tasks_file}` does not exist")

    for ref in re.findall(r'`([^`]+\.(?:md|json))`', claude_text):
        if ref.startswith("make ") or "*" in ref or "<" in ref:
            continue
        if ref.startswith(".agents/") and not Path(ref).exists():
            errors.append(f"CLAUDE.md references `{ref}` which does not exist")

    for path, text in key_texts.items():
        refs = re.findall(r'`([^`]+\.(?:md|json|py))`', text)
        for ref in refs:
            if ref.startswith("make ") or "*" in ref or "<" in ref or "->" in ref:
                continue
            if ref.startswith(".") or "/" in ref:
                if not Path(ref).exists():
                    errors.append(f"{path} references `{ref}` which does not exist")

    persona_names = {agent["name"] for agent in manifest.get("agents", [])}
    for capsule in manifest.get("cross_tool", {}).get("persona_capsules", []):
        if capsule["name"] not in persona_names:
            errors.append(
                f"cross_tool persona capsule '{capsule['name']}' does not match any manifest agent name"
            )

    return errors


def main() -> int:
    check_only = "--check" in sys.argv

    manifest = load_manifest()

    # Check or write trigger-rules.md
    rendered_triggers = render_trigger_rules(manifest)
    trigger_stale = False
    if check_only:
        if TRIGGER_DOC.exists():
            existing = TRIGGER_DOC.read_text(encoding="utf-8")
            trigger_stale = existing != rendered_triggers
        else:
            trigger_stale = True
    else:
        TRIGGER_DOC.write_text(rendered_triggers, encoding="utf-8")
        print(f"Generated {TRIGGER_DOC}")

    # Check or write generated blocks in entry-point files
    stale = render_entry_points(manifest, dry_run=check_only)

    if check_only:
        if trigger_stale:
            stale.insert(0, f"{TRIGGER_DOC}")
        if stale:
            print("FAIL: generated blocks are stale (run 'make render-agent-docs' to fix):")
            for item in stale:
                print(f"  {item}")
            return 1
        print("PASS: generated blocks are up-to-date with manifest.json")
    else:
        print("Updated entry-point generated blocks")

    errors = validate_references(manifest)
    if errors:
        print("\nReference validation errors:")
        for err in errors:
            print(f"  FAIL: {err}")
        return 1

    print("Reference validation: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
