#!/usr/bin/env python3
"""Sync the Topic Registry table in docs/registry.md with source code.

Parses createPublisher<T>("topic") and createSubscription<T>("topic") calls
from node headers, then patches the registry table:
  - Adds missing topics as new rows
  - Adds missing publishers/subscribers to existing rows
  - Adds missing nodes to the Node Inventory table

Does NOT remove entries (human-curated rows like "(reserved)" are preserved).
Does NOT touch the System Data Flow diagram (manual).

Usage:
  python3 .scripts/sync_registry.py          # dry-run (show diff)
  python3 .scripts/sync_registry.py --write  # apply changes
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

NODE_DIR = Path("main/include/chopper/nodes")
REGISTRY = Path("docs/registry.md")

PUB_RE = re.compile(r'createPublisher<(?:messages::)?(\w+)>\s*\(\s*"([a-z]+(?:/[a-z_]+)+)"')
SUB_RE = re.compile(r'createSubscription<(?:messages::)?(\w+)>\s*\(\s*"([a-z]+(?:/[a-z_]+)+)"')
NODE_RE = re.compile(r"\b([A-Z][A-Za-z0-9]+Node)\b")

# Tap nodes subscribe to everything for telemetry — don't list them as regular subscribers
TAP_NODES = {"TelemetryIOTapNode"}

# Stage classification by naming convention
STAGE_RULES = [
    (re.compile(r"BridgeNode$"), "Bridge"),
    (re.compile(r"^Bluepad|InputNode$"), "Input"),
    (re.compile(r"^Safety"), "Safety"),
    (re.compile(r"^Telemetry|^DriverUpdate"), "Telemetry"),
    (re.compile(r".*"), "Action"),  # fallback
]


def classify_stage(node_name: str) -> str:
    for pattern, stage in STAGE_RULES:
        if pattern.search(node_name):
            return stage
    return "Action"


def collect_source_pub_sub() -> dict[str, dict]:
    """Parse all node headers for pub/sub with message types.

    Returns {topic: {"msg_type": str, "publishers": set[str], "subscribers": set[str]}}
    """
    topics: dict[str, dict] = {}

    for header in sorted(NODE_DIR.glob("*.h")):
        node_name = header.stem
        text = header.read_text(encoding="utf-8")

        for match in PUB_RE.finditer(text):
            msg_type, topic = match.group(1), match.group(2)
            topics.setdefault(topic, {"msg_type": msg_type, "publishers": set(), "subscribers": set()})
            topics[topic]["publishers"].add(node_name)
            # Prefer more specific type if already set
            if topics[topic]["msg_type"] != msg_type and msg_type != "auto":
                topics[topic]["msg_type"] = msg_type

        for match in SUB_RE.finditer(text):
            msg_type, topic = match.group(1), match.group(2)
            topics.setdefault(topic, {"msg_type": msg_type, "publishers": set(), "subscribers": set()})
            if node_name not in TAP_NODES:
                topics[topic]["subscribers"].add(node_name)

    return topics


def parse_registry_table(text: str) -> tuple[list[dict], int, int]:
    """Parse the Topic Registry table.

    Returns (rows, table_start_line, table_end_line).
    Each row: {"topic": str, "msg_type": str, "publishers": str, "subscribers": str, "raw": str}
    """
    lines = text.splitlines()
    rows = []
    table_start = -1
    table_end = -1
    in_table = False
    header_seen = False

    for i, line in enumerate(lines):
        if line.startswith("| Topic"):
            in_table = True
            table_start = i
            continue
        if in_table and line.startswith("|---"):
            header_seen = True
            continue
        if in_table and header_seen:
            if not line.startswith("|"):
                table_end = i
                break
            cols = [c.strip() for c in line.split("|")]
            if len(cols) >= 5:
                rows.append({
                    "topic": cols[1].strip("`"),
                    "msg_type": cols[2],
                    "publishers": cols[3],
                    "subscribers": cols[4],
                    "line": i,
                })

    if table_end == -1 and in_table:
        table_end = len(lines)

    return rows, table_start, table_end


def parse_node_inventory(text: str) -> tuple[set[str], int, int]:
    """Find which nodes are in the Node Inventory table. Returns (node_names, start, end)."""
    lines = text.splitlines()
    nodes = set()
    table_start = -1
    table_end = -1
    in_table = False

    for i, line in enumerate(lines):
        if line.startswith("| Node") and "Header" in line:
            in_table = True
            table_start = i
            continue
        if in_table and line.startswith("|---"):
            continue
        if in_table:
            if not line.startswith("|"):
                table_end = i
                break
            for node in NODE_RE.findall(line):
                nodes.add(node)

    if table_end == -1 and in_table:
        table_end = len(lines)

    return nodes, table_start, table_end


def format_node_set(existing_text: str, nodes_to_add: set[str]) -> str:
    """Add node names to an existing publisher/subscriber cell, preserving existing content."""
    existing_nodes = set(NODE_RE.findall(existing_text))
    new_nodes = nodes_to_add - existing_nodes
    if not new_nodes:
        return existing_text

    # If cell is "(reserved)" or similar placeholder and we have real nodes, replace
    if existing_text.strip().startswith("(") and new_nodes:
        return ", ".join(sorted(new_nodes))

    if existing_text.strip() and not existing_text.strip().startswith("("):
        return existing_text.rstrip() + ", " + ", ".join(sorted(new_nodes))
    return ", ".join(sorted(new_nodes))


def sync_registry(write: bool = False) -> int:
    """Sync registry.md with source code. Returns number of changes."""
    text = REGISTRY.read_text(encoding="utf-8")
    lines = text.splitlines()

    source_data = collect_source_pub_sub()
    topic_rows, topic_start, topic_end = parse_registry_table(text)
    inv_nodes, inv_start, inv_end = parse_node_inventory(text)
    header_nodes = {p.stem for p in NODE_DIR.glob("*.h")}

    changes: list[str] = []

    # Build lookup of existing topics
    existing_topics = {row["topic"]: row for row in topic_rows}

    # 1. Update existing topic rows (add missing pubs/subs)
    for row in topic_rows:
        topic = row["topic"]
        if topic not in source_data:
            continue

        src = source_data[topic]
        new_pubs = format_node_set(row["publishers"], src["publishers"])
        new_subs = format_node_set(row["subscribers"], src["subscribers"])

        if new_pubs != row["publishers"] or new_subs != row["subscribers"]:
            old_line = lines[row["line"]]
            new_line = f"| `{topic}` | {row['msg_type']} | {new_pubs} | {new_subs} |"
            lines[row["line"]] = new_line
            changes.append(f"  Updated: {topic} (pubs: {row['publishers']} → {new_pubs}, subs: {row['subscribers']} → {new_subs})")

    # 2. Add new topic rows
    new_rows = []
    for topic, src in sorted(source_data.items()):
        if topic not in existing_topics:
            pubs = ", ".join(sorted(src["publishers"])) or "(reserved)"
            subs = ", ".join(sorted(src["subscribers"])) or "(telemetry)"
            new_row = f"| `{topic}` | {src['msg_type']} | {pubs} | {subs} |"
            new_rows.append(new_row)
            changes.append(f"  Added topic: {topic}")

    if new_rows:
        # Insert before table_end
        for i, row in enumerate(new_rows):
            lines.insert(topic_end + i, row)
        # Adjust inv_start/inv_end for inserted lines
        offset = len(new_rows)
        inv_start += offset
        inv_end += offset

    # 3. Add missing nodes to Node Inventory
    missing_nodes = sorted(header_nodes - inv_nodes)
    new_node_rows = []
    for node in missing_nodes:
        stage = classify_stage(node)
        header_path = f"nodes/{node}.h"
        # Try to extract a brief description from the class doc comment
        header = NODE_DIR / f"{node}.h"
        desc = _extract_brief(header)
        new_node_rows.append(f"| {node} | `{header_path}` | {stage} | {desc} |")
        changes.append(f"  Added node: {node} ({stage})")

    if new_node_rows:
        for i, row in enumerate(new_node_rows):
            lines.insert(inv_end + i, row)

    if not changes:
        print("Registry is in sync with source code.")
        return 0

    print(f"{len(changes)} change(s):")
    for c in changes:
        print(c)

    if write:
        new_text = "\n".join(lines)
        # Ensure trailing newline
        if not new_text.endswith("\n"):
            new_text += "\n"
        REGISTRY.write_text(new_text, encoding="utf-8")
        print(f"\nWrote {REGISTRY}")
    else:
        print(f"\nDry run — use --write to apply.")

    return len(changes)


def _extract_brief(header: Path) -> str:
    """Extract a one-line description from a node header's doc comment or class name."""
    if not header.exists():
        return ""
    text = header.read_text(encoding="utf-8")

    # Look for /** ... */ or /// before the class
    # Try @brief or first sentence after /**
    for m in re.finditer(r'/\*\*\s*\n?\s*\*?\s*(.+?)(?:\n|\*/)', text):
        brief = m.group(1).strip().rstrip(".")
        if brief and not brief.startswith("@"):
            return brief
        if brief.startswith("@brief"):
            return brief.replace("@brief", "").strip().rstrip(".")

    # Try /// comments before class
    for m in re.finditer(r'///\s*(.+)', text):
        brief = m.group(1).strip().rstrip(".")
        if brief and "param" not in brief.lower():
            return brief

    return ""


def main() -> int:
    write = "--write" in sys.argv
    count = sync_registry(write=write)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
