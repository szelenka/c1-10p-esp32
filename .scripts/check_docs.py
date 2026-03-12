#!/usr/bin/env python3
"""Detect stale documentation using source-aware checks."""

from __future__ import annotations

import re
from pathlib import Path

from gate_lib import GateReporter
from render_agent_docs import render_trigger_rules


REGISTRY = Path("docs/registry.md")
LIMITS_H = Path("main/include/chopper/chopper_limits.h")
ARCHITECT_MD = Path(".agents/extended.md")
NODE_DIR = Path("main/include/chopper/nodes")
SOURCE_DIRS = [Path("main/include/chopper"), Path("main/chopper")]
AGENT_MANIFEST = Path(".agents/manifest.json")
TRIGGER_DOC = Path(".agents/generated/trigger-rules.md")
TOPIC_RE = re.compile(r'"([a-z]+(?:/[a-z_]+)+)"')
LIMIT_RE = re.compile(r"(MAX_[A-Z_]+)\s*=\s*([0-9]+)")
NODE_RE = re.compile(r"\b([A-Z][A-Za-z0-9]+Node)\b")
TOPIC_CONTEXT_RE = re.compile(
    r"createPublisher<|createSubscription<|addPublisher\(|addSubscription\(|topic",
)


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


PUB_RE = re.compile(r"createPublisher<[^>]+>\(\"([a-z]+(?:/[a-z_]+)+)\"")
SUB_RE = re.compile(r"createSubscription<[^>]+>\(\"([a-z]+(?:/[a-z_]+)+)\"")


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


def collect_code_pub_sub() -> dict[str, dict[str, set[str]]]:
    """Collect per-node publisher and subscriber topic sets from source code."""
    result: dict[str, dict[str, set[str]]] = {}
    for header in NODE_DIR.glob("*.h"):
        node_name = header.stem
        pubs: set[str] = set()
        subs: set[str] = set()
        text = read_text(header)
        for match in PUB_RE.finditer(text):
            pubs.add(match.group(1))
        for match in SUB_RE.finditer(text):
            subs.add(match.group(1))
        if pubs or subs:
            result[node_name] = {"publishes": pubs, "subscribes": subs}
    return result


def collect_registry_pub_sub() -> dict[str, dict[str, set[str]]]:
    """Parse the Topic Registry table to extract per-node pub/sub from the registry."""
    node_pubs: dict[str, set[str]] = {}
    node_subs: dict[str, set[str]] = {}
    in_table = False
    for line in read_text(REGISTRY).splitlines():
        if line.startswith("| Topic"):
            in_table = True
            continue
        if line.startswith("|---"):
            continue
        if in_table and line.startswith("|"):
            cols = [c.strip() for c in line.split("|")]
            if len(cols) < 5:
                continue
            topic = cols[1].strip("`")
            publishers_col = cols[3]
            subscribers_col = cols[4]
            for node in NODE_RE.findall(publishers_col):
                node_pubs.setdefault(node, set()).add(topic)
            for node in NODE_RE.findall(subscribers_col):
                node_subs.setdefault(node, set()).add(topic)
        elif in_table and not line.startswith("|"):
            in_table = False

    result: dict[str, dict[str, set[str]]] = {}
    all_nodes = set(node_pubs) | set(node_subs)
    for node in all_nodes:
        result[node] = {
            "publishes": node_pubs.get(node, set()),
            "subscribes": node_subs.get(node, set()),
        }
    return result


def collect_header_nodes() -> set[str]:
    return {path.stem for path in NODE_DIR.glob("*.h")}


def collect_manifest() -> dict:
    import json

    return json.loads(read_text(AGENT_MANIFEST))


def main() -> int:
    reporter = GateReporter("check-docs")

    # Node inventory vs headers
    if REGISTRY.exists() and NODE_DIR.is_dir():
        header_nodes = collect_header_nodes()
        registry_nodes = set(NODE_RE.findall(read_text(REGISTRY)))
        missing_from_registry = sorted(header_nodes - registry_nodes)
        missing_from_code = sorted(registry_nodes - header_nodes)

        if missing_from_registry:
            for node in missing_from_registry:
                header = NODE_DIR / f"{node}.h"
                reporter.fail(
                    "node-inventory", str(header.relative_to(".")), 0,
                    f"header exists but not in registry: {node}",
                )
        if missing_from_code:
            for node in missing_from_code:
                reporter.fail(
                    "node-inventory", "docs/registry.md", 0,
                    f"in registry but no header found: {node}",
                )
        if not missing_from_registry and not missing_from_code:
            reporter.pass_("node-inventory", message=f"Node inventory matches headers ({len(header_nodes)} nodes)")
    else:
        reporter.skip("registry or node dir not found")

    # chopper_limits.h vs extended.md Resource Budget
    if LIMITS_H.exists() and ARCHITECT_MD.exists():
        architect_text = read_text(ARCHITECT_MD)
        stale_limits: list[str] = []
        for name, value in LIMIT_RE.findall(read_text(LIMITS_H)):
            if name in architect_text and not re.search(rf"(^|[^0-9]){value}([^0-9]|$)", architect_text):
                stale_limits.append(f"{name}({value})")
        if stale_limits:
            reporter.fail(
                "limit-values", str(ARCHITECT_MD), 0,
                f"limit values stale in extended.md: {', '.join(stale_limits)}",
            )
        else:
            reporter.pass_("limit-values", message="Limit values consistent between chopper_limits.h and extended.md")
    else:
        reporter.skip("limits header or extended.md not found")

    # Topic names in source vs registry
    if REGISTRY.exists():
        code_topics = collect_code_topics()
        registry_topics, reserved_topics = collect_registry_topics()
        missing_from_registry = sorted(code_topics - registry_topics)
        missing_from_code = sorted((registry_topics - reserved_topics) - code_topics)
        if missing_from_registry:
            for topic in missing_from_registry:
                reporter.fail(
                    "topic-names", "docs/registry.md", 0,
                    f"topic in code but not in registry: {topic}",
                )
        if missing_from_code:
            for topic in missing_from_code:
                reporter.fail(
                    "topic-names", "docs/registry.md", 0,
                    f"topic in registry but not in code: {topic}",
                )
        if not missing_from_registry and not missing_from_code:
            reporter.pass_("topic-names", message=f"Registry matches source topics ({len(code_topics)} topics)")
    else:
        reporter.skip("registry not found")

    # Nodes that intentionally tap all topics — skip subscriber cross-check
    TAP_NODES = {"TelemetryIOTapNode"}

    # Registry pub/sub vs source code
    if REGISTRY.exists() and NODE_DIR.is_dir():
        code_ps = collect_code_pub_sub()
        reg_ps = collect_registry_pub_sub()
        mismatches: list[str] = []
        for node_name, code_info in code_ps.items():
            reg_info = reg_ps.get(node_name, {"publishes": set(), "subscribes": set()})
            missing_pubs = code_info["publishes"] - reg_info["publishes"]
            if missing_pubs:
                header = NODE_DIR / f"{node_name}.h"
                reporter.fail(
                    "pub-sub", str(header.relative_to(".")), 0,
                    f"{node_name} publishes {sorted(missing_pubs)} but registry does not list it",
                )
            if node_name not in TAP_NODES:
                missing_subs = code_info["subscribes"] - reg_info["subscribes"]
                if missing_subs:
                    header = NODE_DIR / f"{node_name}.h"
                    reporter.fail(
                        "pub-sub", str(header.relative_to(".")), 0,
                        f"{node_name} subscribes {sorted(missing_subs)} but registry does not list it",
                    )
        if not any(f.status == "FAIL" and f.context == "pub-sub" for f in reporter.findings):
            reporter.pass_("pub-sub", message="Registry pub/sub columns match source code")
    else:
        reporter.skip("registry or node dir not found")

    # Generated trigger-rules.md vs manifest
    if AGENT_MANIFEST.exists() and TRIGGER_DOC.exists():
        manifest = collect_manifest()
        expected_trigger = render_trigger_rules(manifest)
        if read_text(TRIGGER_DOC) != expected_trigger:
            reporter.fail(
                "trigger-rules", str(TRIGGER_DOC), 0,
                "trigger-rules.md is stale (run `make render-agent-docs`)",
            )
        else:
            reporter.pass_("trigger-rules", message="trigger-rules.md matches .agents/manifest.json")
    else:
        reporter.skip("manifest or trigger-rules.md not found")

    return reporter.finish()


if __name__ == "__main__":
    raise SystemExit(main())
