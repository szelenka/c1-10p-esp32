#!/usr/bin/env python3
"""Transitive safety reachability analysis.

Parses docs/registry.md to build a topic-level graph, then checks whether
any changed file can transitively reach an actuator (MotorBridgeNode,
ServoBridgeNode) through pub/sub topics.

Usage:
  python3 .scripts/check_safety_reachability.py              # check git diff
  python3 .scripts/check_safety_reachability.py file1 file2  # check specific files

Exit codes:
  0  No actuator-reachable changes detected
  1  At least one changed file can transitively reach an actuator
"""

from __future__ import annotations

import re
import subprocess
import sys
from collections import defaultdict, deque
from pathlib import Path

from gate_lib import GateReporter

REGISTRY = Path("docs/registry.md")
NODE_DIR = Path("main/include/chopper/nodes")

# Bridge nodes that reach physical actuators
ACTUATOR_NODES = {"MotorBridgeNode", "ServoBridgeNode", "OpenMvBridgeNode"}

# Map header basenames to node names (e.g. "DriveNode.h" -> "DriveNode")
def header_to_node(header: str) -> str:
    return Path(header).stem


def parse_registry_topics(registry_text: str) -> list[dict]:
    """Parse the Topic Registry table from registry.md."""
    topics = []
    in_table = False
    header_seen = False

    for line in registry_text.splitlines():
        if "| Topic |" in line and "Publisher" in line:
            in_table = True
            continue
        if in_table and line.startswith("|---"):
            header_seen = True
            continue
        if in_table and header_seen:
            if not line.startswith("|"):
                break
            cols = [c.strip() for c in line.split("|")]
            if len(cols) >= 6:
                topic = cols[1].strip("`")
                publishers = cols[3]
                subscribers = cols[4]
                topics.append({
                    "topic": topic,
                    "publishers": parse_node_list(publishers),
                    "subscribers": parse_node_list(subscribers),
                })
    return topics


def parse_node_list(cell: str) -> list[str]:
    """Extract node names from a registry table cell."""
    cell = re.sub(r"\(.*?\)", "", cell)
    cell = re.sub(r"[`*]", "", cell)
    nodes = []
    for part in re.split(r"[,/]", cell):
        part = part.strip()
        if part and part != "(reserved)" and "aggregate" not in part.lower():
            part = re.sub(r"\(s\)$", "", part)
            if re.match(r"^[A-Z]", part):
                nodes.append(part)
    return nodes


def build_reachability_graph(topics: list[dict]) -> dict[str, set[str]]:
    """Build a directed graph: node -> set of nodes it can reach via topics."""
    graph: dict[str, set[str]] = defaultdict(set)
    for topic in topics:
        for pub in topic["publishers"]:
            for sub in topic["subscribers"]:
                if pub != sub:
                    graph[pub].add(sub)
    return graph


def find_reachable(graph: dict[str, set[str]], start: str) -> set[str]:
    """BFS from start node, return all reachable nodes."""
    visited = set()
    queue = deque([start])
    while queue:
        node = queue.popleft()
        if node in visited:
            continue
        visited.add(node)
        for neighbor in graph.get(node, set()):
            if neighbor not in visited:
                queue.append(neighbor)
    return visited


def find_path(graph: dict[str, set[str]], start: str, target_set: set[str]) -> list[str] | None:
    """BFS shortest path from start to any node in target_set."""
    if start in target_set:
        return [start]
    visited = {start}
    queue = deque([(start, [start])])
    while queue:
        node, path = queue.popleft()
        for neighbor in graph.get(node, set()):
            if neighbor in target_set:
                return path + [neighbor]
            if neighbor not in visited:
                visited.add(neighbor)
                queue.append((neighbor, path + [neighbor]))
    return None


def changed_files(explicit_files: list[str] | None = None) -> list[str]:
    """Get list of changed files from git or explicit arguments."""
    if explicit_files:
        return explicit_files

    result = subprocess.run(
        ["git", "diff", "--name-only", "--diff-filter=d", "HEAD"],
        capture_output=True, text=True, check=False,
    )
    staged = subprocess.run(
        ["git", "diff", "--name-only", "--diff-filter=d", "--cached"],
        capture_output=True, text=True, check=False,
    )
    files = set(result.stdout.strip().split("\n") + staged.stdout.strip().split("\n"))
    return [f for f in files if f.strip()]


def file_to_nodes(filepath: str) -> list[str]:
    """Map a file path to the node(s) it might affect."""
    nodes = []
    basename = Path(filepath).stem

    if filepath.startswith("main/include/chopper/nodes/") and filepath.endswith(".h"):
        nodes.append(basename)

    if "IntentMapping" in basename or "Mapping" in basename:
        nodes.append("BluepadInputNode")

    if filepath.startswith("main/chopper/") and filepath.endswith(".cpp"):
        candidate = basename
        if candidate.endswith("Node") or "Bridge" in candidate:
            nodes.append(candidate)

    if "/safety/" in filepath:
        nodes.extend(["SafetyNode", "MotorBridgeNode", "ServoBridgeNode"])

    if "/hal/" in filepath or "/adapters/" in filepath:
        nodes.extend(["MotorBridgeNode", "ServoBridgeNode", "AudioBridgeNode"])

    if "CommonMessages.h" in filepath:
        nodes.append("__ALL__")

    return nodes


def main() -> int:
    reporter = GateReporter("check-safety-reachability")

    if not REGISTRY.exists():
        reporter.skip("docs/registry.md not found")
        return reporter.finish()

    registry_text = REGISTRY.read_text(encoding="utf-8")
    topics = parse_registry_topics(registry_text)

    if not topics:
        reporter.warn("registry", "docs/registry.md", 0, "No topics parsed from registry.md")
        return reporter.finish()

    graph = build_reachability_graph(topics)

    all_nodes = set()
    for topic in topics:
        all_nodes.update(topic["publishers"])
        all_nodes.update(topic["subscribers"])

    explicit = sys.argv[1:] if len(sys.argv) > 1 else None
    files = changed_files(explicit)

    if not files or files == [""]:
        reporter.pass_("reachability", message="No changed files to analyze")
        return reporter.finish()

    actuator_reachable_files: list[tuple[str, str, list[str]]] = []

    for filepath in sorted(files):
        nodes = file_to_nodes(filepath)
        if not nodes:
            continue

        if "__ALL__" in nodes:
            nodes = list(all_nodes)

        for node in nodes:
            if node not in all_nodes and node not in ACTUATOR_NODES:
                continue

            if node in ACTUATOR_NODES:
                actuator_reachable_files.append((filepath, node, [node]))
                continue

            path = find_path(graph, node, ACTUATOR_NODES)
            if path:
                actuator_reachable_files.append((filepath, node, path))

    if actuator_reachable_files:
        seen = set()
        for filepath, node, path in actuator_reachable_files:
            key = (filepath, node)
            if key in seen:
                continue
            seen.add(key)
            path_str = " -> ".join(path)
            reporter.fail(
                "reachability", filepath, 0,
                f"actuator-reachable via {node}: {path_str}",
                severity="BLOCKING",
            )
    else:
        reporter.pass_("reachability", message="No actuator-reachable changes detected")

    return reporter.finish()


if __name__ == "__main__":
    raise SystemExit(main())
