#!/usr/bin/env python3
"""Detect stale documentation using source-aware checks."""

from __future__ import annotations

import re
from pathlib import Path

from render_agent_docs import render_agent_team, render_golden_tasks, render_trigger_rules


REGISTRY = Path("docs/registry.md")
LIMITS_H = Path("main/include/chopper/chopper_limits.h")
ARCHITECT_MD = Path(".agents/personas/architect.md")
NODE_DIR = Path("main/include/chopper/nodes")
SOURCE_DIRS = [Path("main/include/chopper"), Path("main/chopper")]
AGENT_MANIFEST = Path(".agents/manifest.json")
TRIGGER_DOC = Path(".agents/generated/trigger-rules.md")
GOLDEN_DOC = Path(".agents/generated/golden-tasks.md")
AGENT_TEAM_DOC = Path(".agents/generated/agent-team.md")
TOPIC_RE = re.compile(r'"([a-z]+(?:/[a-z_]+)+)"')
LIMIT_RE = re.compile(r"(MAX_[A-Z_]+)\s*=\s*([0-9]+)")
NODE_RE = re.compile(r"\b([A-Z][A-Za-z0-9]+Node)\b")
TOPIC_CONTEXT_RE = re.compile(
    r"createPublisher<|createSubscription<|addPublisher\(|addSubscription\(|topic",
)


class CheckState:
    def __init__(self) -> None:
        self.exit_code = 0
        self.warn_count = 0

    def check_pass(self, message: str) -> None:
        print(f"  PASS: {message}")

    def check_fail(self, message: str) -> None:
        print(f"  FAIL: {message}")
        self.exit_code = 1


def read_text(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def collect_registry_topics() -> tuple[set[str], set[str]]:
    topics: set[str] = set()
    reserved_topics: set[str] = set()
    for line in read_text(REGISTRY).splitlines():
        matches = re.findall(r"`([a-z]+(?:/[a-z_]+)+)`", line)
        for topic in matches:
            topics.add(topic)
            if "reserved" in line.lower():
                reserved_topics.add(topic)
    return topics, reserved_topics


def collect_code_topics() -> set[str]:
    topics: set[str] = set()
    for root in SOURCE_DIRS:
        for path in root.rglob("*.[hc]pp"):
            for line in read_text(path).splitlines():
                if not TOPIC_CONTEXT_RE.search(line):
                    continue
                topics.update(TOPIC_RE.findall(line))
        for path in root.rglob("*.h"):
            for line in read_text(path).splitlines():
                if not TOPIC_CONTEXT_RE.search(line):
                    continue
                topics.update(TOPIC_RE.findall(line))
    return topics


def collect_header_nodes() -> set[str]:
    return {path.stem for path in NODE_DIR.glob("*.h")}


def collect_manifest() -> dict:
    import json

    return json.loads(read_text(AGENT_MANIFEST))


def main() -> int:
    state = CheckState()

    print("== Node inventory vs headers ==")
    if REGISTRY.exists() and NODE_DIR.is_dir():
        header_nodes = collect_header_nodes()
        registry_nodes = set(NODE_RE.findall(read_text(REGISTRY)))
        missing_from_registry = sorted(header_nodes - registry_nodes)
        missing_from_code = sorted(registry_nodes - header_nodes)

        if missing_from_registry:
            state.check_fail(
                "Headers exist but not in registry: " + ", ".join(missing_from_registry)
            )
        if missing_from_code:
            state.check_fail(
                "In registry but no header found: " + ", ".join(missing_from_code)
            )
        if not missing_from_registry and not missing_from_code:
            state.check_pass(f"Node inventory matches headers ({len(header_nodes)} nodes)")
    else:
        print("  SKIP: registry or node dir not found")

    print("\n== chopper_limits.h vs architect.md Resource Budget ==")
    if LIMITS_H.exists() and ARCHITECT_MD.exists():
        architect_text = read_text(ARCHITECT_MD)
        stale_limits: list[str] = []
        for name, value in LIMIT_RE.findall(read_text(LIMITS_H)):
            if name in architect_text and not re.search(rf"(^|[^0-9]){value}([^0-9]|$)", architect_text):
                stale_limits.append(f"{name}({value})")
        if stale_limits:
            state.check_fail("Limit values are stale in architect.md: " + ", ".join(stale_limits))
        else:
            state.check_pass("Limit values consistent between chopper_limits.h and architect.md")
    else:
        print("  SKIP: limits header or architect.md not found")

    print("\n== Topic names in source vs registry ==")
    if REGISTRY.exists():
        code_topics = collect_code_topics()
        registry_topics, reserved_topics = collect_registry_topics()
        missing_from_registry = sorted(code_topics - registry_topics)
        missing_from_code = sorted((registry_topics - reserved_topics) - code_topics)
        if missing_from_registry:
            state.check_fail("Topics in code but not in registry: " + ", ".join(missing_from_registry))
        if missing_from_code:
            state.check_fail("Topics in registry but not in code: " + ", ".join(missing_from_code))
        if not missing_from_registry and not missing_from_code:
            state.check_pass(f"Registry matches source topics ({len(code_topics)} topics)")
    else:
        print("  SKIP: registry not found")

    print("\n== Generated agent docs vs manifest ==")
    if AGENT_MANIFEST.exists() and TRIGGER_DOC.exists() and GOLDEN_DOC.exists():
        manifest = collect_manifest()
        expected_trigger = render_trigger_rules(manifest)
        expected_golden = render_golden_tasks(manifest)
        expected_team = render_agent_team(manifest)
        stale_docs: list[str] = []
        if read_text(TRIGGER_DOC) != expected_trigger:
            stale_docs.append(str(TRIGGER_DOC))
        if read_text(GOLDEN_DOC) != expected_golden:
            stale_docs.append(str(GOLDEN_DOC))
        if AGENT_TEAM_DOC.exists() and read_text(AGENT_TEAM_DOC) != expected_team:
            stale_docs.append(str(AGENT_TEAM_DOC))
        if stale_docs:
            state.check_fail(
                "Generated agent docs are stale: "
                + ", ".join(stale_docs)
                + " (run `make render-agent-docs`)"
            )
        else:
            state.check_pass("Generated agent docs match .agents/manifest.json")
    else:
        print("  SKIP: manifest or generated agent docs not found")

    print("")
    if state.exit_code == 0 and state.warn_count == 0:
        print("== All doc checks passed ==")
    else:
        print("== DOC STALENESS DETECTED ==")
    return state.exit_code


if __name__ == "__main__":
    raise SystemExit(main())
