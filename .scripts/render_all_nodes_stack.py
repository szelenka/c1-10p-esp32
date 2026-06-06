#!/usr/bin/env python3
"""Generate the all-node horizontal stack diagram layout from docs/registry.md.

This script owns docs/visualizations/all-nodes-horizontal-stack.layout.yml.
The SVG renderer then turns that YAML layout into SVG/PNG artifacts.
"""

from __future__ import annotations

import argparse
import difflib
import re
import sys
import textwrap
from pathlib import Path
from typing import Any

import yaml


REGISTRY = Path("docs/registry.md")
OUTPUT = Path("docs/visualizations/all-nodes-horizontal-stack.layout.yml")

NODE_RE = re.compile(r"\b([A-Z][A-Za-z0-9]+Node)\b")

CANVAS_WIDTH = 1840
TOP_Y = 170
BOX_H = 96
GAP_Y = 10
TEXT_X_PAD = 18
TITLE_Y_PAD = 36
LINE_Y_PAD = 62
LINE_SPACING = 22

COLUMN_DEFS = [
    {
        "key": "input",
        "title": "1. Input",
        "x": 55,
        "width": 220,
        "stages": ["Input"],
        "class": "input",
    },
    {
        "key": "controller_topics",
        "title": "2. Controller topics",
        "x": 310,
        "width": 205,
        "topic_group": "controller",
        "class": "topic",
    },
    {
        "key": "action",
        "title": "3. Action nodes",
        "x": 555,
        "width": 245,
        "stages": ["Action"],
        "class": "action",
    },
    {
        "key": "command_topics",
        "title": "4. Command topics",
        "x": 850,
        "width": 220,
        "topic_group": "command",
        "class": "topic",
    },
    {
        "key": "bridge_safety",
        "title": "5. Bridge + safety",
        "x": 1105,
        "width": 225,
        "stages": ["Safety", "Bridge"],
        "class_by_stage": {"Safety": "bridge", "Bridge": "bridge"},
    },
    {
        "key": "hardware_driver",
        "title": "6. Driver/external",
        "x": 1375,
        "width": 220,
        "stages": ["Driver"],
        "class": "hardware",
    },
]

TELEMETRY_BAND_X = 850
TELEMETRY_NODE_X = 1105
TELEMETRY_NODE_WIDTH = 225
TELEMETRY_NODE_GAP_X = 20

EXTERNAL_BOXES = [
    {
        "title": "Motor drivers",
        "lines": ["Sabertooth feet", "SyRen dome"],
        "refs": {"topics": ["drive/cmd", "dome/motor/cmd"]},
    },
    {
        "title": "Maestro controllers",
        "lines": ["Body servos", "Dome servos"],
        "refs": {"topics": ["servo/body/cmd", "servo/dome/cmd"]},
    },
    {
        "title": "MP3 Trigger",
        "lines": ["Tracks and volume"],
        "refs": {"topics": ["audio/cmd"]},
    },
    {
        "title": "OpenMV camera",
        "lines": ["Serial command sink", "Vision source"],
        "refs": {"topics": ["openmv/tracking/cmd", "vision/result"]},
    },
]


def read_text(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def ascii_text(value: str) -> str:
    replacements = {
        "\u2192": "->",
        "\u2194": "<->",
        "\u2014": "-",
        "\u2013": "-",
        "\u2018": "'",
        "\u2019": "'",
        "\u201c": '"',
        "\u201d": '"',
    }
    for src, dst in replacements.items():
        value = value.replace(src, dst)
    return value.encode("ascii", "ignore").decode("ascii")


def clean_cell(value: str) -> str:
    value = re.sub(r"`([^`]+)`", r"\1", value)
    value = re.sub(r"\s+", " ", value)
    return ascii_text(value).strip()


def parse_markdown_table(text: str, header_prefix: str) -> list[list[str]]:
    rows: list[list[str]] = []
    in_table = False
    separator_seen = False

    for line in text.splitlines():
        if line.startswith(header_prefix):
            in_table = True
            separator_seen = False
            continue
        if in_table and line.startswith("|---"):
            separator_seen = True
            continue
        if in_table and separator_seen and line.startswith("|"):
            cols = [clean_cell(col) for col in line.strip().strip("|").split("|")]
            rows.append(cols)
            continue
        if in_table and separator_seen:
            break

    return rows


def parse_registry(path: Path) -> tuple[list[dict[str, str]], list[dict[str, str]]]:
    text = read_text(path)

    topic_rows: list[dict[str, str]] = []
    for cols in parse_markdown_table(text, "| Topic"):
        if len(cols) >= 4:
            topic_rows.append({
                "topic": cols[0],
                "message_type": cols[1],
                "publishers": cols[2],
                "subscribers": cols[3],
            })

    node_rows: list[dict[str, str]] = []
    for cols in parse_markdown_table(text, "| Node"):
        if len(cols) >= 4:
            node_rows.append({
                "node": cols[0],
                "header": cols[1],
                "stage": cols[2],
                "responsibility": cols[3],
            })

    if not node_rows:
        raise ValueError(f"No node inventory rows found in {path}")
    return node_rows, topic_rows


def wrap_summary(text: str, max_chars: int = 25, max_lines: int = 2) -> list[str]:
    first_sentence = re.split(r"(?<=[.!?])\s+", clean_cell(text))[0]
    first_sentence = re.sub(r"\bNever\b.*", "", first_sentence).strip()
    if not first_sentence:
        first_sentence = "Registered node"

    wrapped = textwrap.wrap(first_sentence, width=max_chars, break_long_words=False)
    if len(wrapped) <= max_lines:
        return wrapped

    kept = wrapped[:max_lines]
    if not kept[-1].endswith("..."):
        kept[-1] = kept[-1].rstrip(".,;:") + "..."
    return kept


def topic_groups(topic_rows: list[dict[str, str]]) -> dict[str, list[str]]:
    controller: list[str] = []
    command: list[str] = []
    status: list[str] = []

    for row in topic_rows:
        topic = row["topic"]
        if topic.startswith("controller/"):
            controller.append(topic)
        elif topic.endswith("/cmd") or topic.endswith("/move"):
            command.append(topic)
        else:
            status.append(topic)

    return {
        "controller": controller,
        "command": command,
        "status": status,
    }


def box_item(
    *,
    cls: str,
    x: int,
    y: int,
    width: int,
    height: int,
    title: str,
    lines: list[str] | None = None,
    refs: dict[str, list[str]] | None = None,
) -> dict[str, Any]:
    item: dict[str, Any] = {
        "kind": "box",
        "class": cls,
        "x": x,
        "y": y,
        "width": width,
        "height": height,
        "title": {"x": x + TEXT_X_PAD, "y": y + TITLE_Y_PAD, "text": title},
    }
    if refs:
        item["refs"] = refs
    if lines:
        item["lines"] = [
            {"x": x + TEXT_X_PAD, "y": y + LINE_Y_PAD + (idx * LINE_SPACING), "text": line}
            for idx, line in enumerate(lines)
        ]
    return item


def text_item(cls: str, x: int, y: int, text: str) -> dict[str, Any]:
    return {"kind": "text", "class": cls, "x": x, "y": y, "text": text}


def path_item(cls: str, d: str) -> dict[str, Any]:
    return {"kind": "path", "class": cls, "d": d}


def circle_item(cls: str, cx: int, cy: int, r: int = 4) -> dict[str, Any]:
    return {"kind": "circle", "class": cls, "cx": cx, "cy": cy, "r": r}


def group_nodes_by_stage(node_rows: list[dict[str, str]]) -> dict[str, list[dict[str, str]]]:
    grouped: dict[str, list[dict[str, str]]] = {}
    for row in node_rows:
        grouped.setdefault(row["stage"], []).append(row)
    return grouped


def add_node_column(
    items: list[dict[str, Any]],
    col: dict[str, Any],
    grouped: dict[str, list[dict[str, str]]],
) -> list[dict[str, Any]]:
    boxes: list[dict[str, Any]] = []
    y = TOP_Y

    for stage in col["stages"]:
        for row in grouped.get(stage, []):
            cls = col.get("class_by_stage", {}).get(stage, col.get("class", "note"))
            box = box_item(
                cls=cls,
                x=col["x"],
                y=y,
                width=col["width"],
                height=BOX_H,
                title=row["node"],
                lines=wrap_summary(row["responsibility"], max_chars=28),
                refs={"nodes": [row["node"]]},
            )
            items.append(box)
            boxes.append(box)
            y += BOX_H + GAP_Y

    return boxes


def add_topic_group_column(
    items: list[dict[str, Any]],
    col: dict[str, Any],
    topics: dict[str, list[str]],
) -> list[dict[str, Any]]:
    group = col["topic_group"]
    topic_list = topics.get(group, [])
    if not topic_list:
        return []

    title_by_group = {
        "controller": "controller/*",
        "command": "command topics",
        "status": "status + result",
    }
    lines_by_group = {
        "controller": [f"{len(topic_list)} controller routes"],
        "command": [f"{len(topic_list)} command/move routes"],
        "status": [f"{len(topic_list)} observer routes"],
    }

    y = TOP_Y
    box = box_item(
        cls=col["class"],
        x=col["x"],
        y=y,
        width=col["width"],
        height=BOX_H,
        title=title_by_group[group],
        lines=lines_by_group[group],
        refs={"topics": topic_list},
    )
    items.append(box)
    return [box]


def add_external_boxes(items: list[dict[str, Any]], x: int, width: int, start_y: int) -> list[dict[str, Any]]:
    boxes: list[dict[str, Any]] = []
    y = start_y
    for spec in EXTERNAL_BOXES:
        box = box_item(
            cls="hardware",
            x=x,
            y=y,
            width=width,
            height=BOX_H,
            title=spec["title"],
            lines=spec["lines"],
            refs=spec.get("refs"),
        )
        items.append(box)
        boxes.append(box)
        y += BOX_H + GAP_Y
    return boxes


def center_y(boxes: list[dict[str, Any]]) -> int:
    if not boxes:
        return TOP_Y + BOX_H // 2
    first = boxes[0]
    last = boxes[-1]
    return int((first["y"] + (last["y"] + last["height"])) / 2)


def right_x(box: dict[str, Any]) -> int:
    return int(box["x"] + box["width"])


def left_x(box: dict[str, Any]) -> int:
    return int(box["x"])


def add_stage_flow(
    items: list[dict[str, Any]],
    columns: dict[str, list[dict[str, Any]]],
) -> None:
    ordered = [
        ("input", "controller_topics", "line"),
        ("controller_topics", "action", "drive-line"),
        ("action", "command_topics", "line"),
        ("command_topics", "bridge_safety", "line"),
        ("bridge_safety", "hardware_driver", "line"),
    ]

    for src_key, dst_key, cls in ordered:
        src_boxes = columns.get(src_key, [])
        dst_boxes = columns.get(dst_key, [])
        if not src_boxes or not dst_boxes:
            continue
        src = src_boxes[0]
        dst = dst_boxes[0]
        y = center_y([src])
        dst_y = center_y([dst])
        mid_x = int((right_x(src) + left_x(dst)) / 2)
        items.append(path_item(cls, f"M {right_x(src)} {y} L {mid_x} {y} L {mid_x} {dst_y} L {left_x(dst)} {dst_y}"))


def render_layout(registry_path: Path) -> str:
    node_rows, topic_rows = parse_registry(registry_path)
    grouped = group_nodes_by_stage(node_rows)
    topics = topic_groups(topic_rows)

    max_column_count = max(
        len(grouped.get("Action", [])),
        len(grouped.get("Bridge", [])) + len(grouped.get("Safety", [])),
        len(grouped.get("Driver", [])) + len(EXTERNAL_BOXES),
        8,
    )
    content_bottom = TOP_Y + (max_column_count * (BOX_H + GAP_Y))
    telemetry_y = content_bottom + 30
    telemetry_rows = max(1, (len(grouped.get("Telemetry", [])) + 2) // 3)
    telemetry_bottom = telemetry_y + (telemetry_rows * (BOX_H + GAP_Y))
    note_y = telemetry_bottom + 35
    canvas_height = max(1040, note_y + 236)

    items: list[dict[str, Any]] = [
        text_item("title", 55, 56, "All registered nodes, stacked left to right"),
        text_item(
            "subtitle",
            55,
            88,
            "Generated from docs/registry.md: node inventory by stage, representative topic flow, and external sinks.",
        ),
    ]

    for col in COLUMN_DEFS:
        items.append(text_item("band-title", col["x"], 142, col["title"]))

    rendered_columns: dict[str, list[dict[str, Any]]] = {}
    for col in COLUMN_DEFS:
        if "topic_group" in col:
            rendered_columns[col["key"]] = add_topic_group_column(items, col, topics)
        else:
            rendered_columns[col["key"]] = add_node_column(items, col, grouped)

    hardware_col = next(col for col in COLUMN_DEFS if col["key"] == "hardware_driver")
    external_start_y = TOP_Y + max(1, len(rendered_columns["hardware_driver"])) * (BOX_H + GAP_Y)
    rendered_columns["hardware_driver"].extend(
        add_external_boxes(items, hardware_col["x"], hardware_col["width"], external_start_y)
    )

    items.append(text_item("band-title", TELEMETRY_BAND_X, telemetry_y - 28, "7. Telemetry"))
    status_topics = topics.get("status", [])
    status_box: dict[str, Any] | None = None
    if status_topics:
        status_box = box_item(
            cls="topic",
            x=TELEMETRY_BAND_X,
            y=telemetry_y,
            width=220,
            height=BOX_H,
            title="status + result",
            lines=[f"{len(status_topics)} observer routes"],
            refs={"topics": status_topics},
        )
        items.append(status_box)
        rendered_columns["status_topics"] = [status_box]

    telemetry_rows_data = sorted(
        grouped.get("Telemetry", []),
        key=lambda row: (0 if row["node"] == "TelemetryIOTapNode" else 1, row["node"]),
    )
    telemetry_boxes: list[dict[str, Any]] = []
    for idx, row in enumerate(telemetry_rows_data):
        x = TELEMETRY_NODE_X + ((idx % 3) * (TELEMETRY_NODE_WIDTH + TELEMETRY_NODE_GAP_X))
        y = telemetry_y + ((idx // 3) * (BOX_H + GAP_Y))
        box = box_item(
            cls="telemetry",
            x=x,
            y=y,
            width=TELEMETRY_NODE_WIDTH,
            height=BOX_H,
            title=row["node"],
            lines=wrap_summary(row["responsibility"], max_chars=28),
            refs={"nodes": [row["node"]]},
        )
        items.append(box)
        telemetry_boxes.append(box)
    rendered_columns["telemetry"] = telemetry_boxes

    add_stage_flow(items, rendered_columns)
    command_boxes = rendered_columns.get("command_topics", [])
    if command_boxes and status_box:
        command = command_boxes[0]
        x = left_x(command) + int(command["width"] / 2)
        items.append(path_item("telemetry-flow", f"M {x} {command['y'] + command['height']} L {x} {status_box['y']}"))
    if status_box and telemetry_boxes:
        items.append(path_item("telemetry-flow", f"M {right_x(status_box)} {center_y([status_box])} L {left_x(telemetry_boxes[0])} {center_y([telemetry_boxes[0]])}"))
    if len(telemetry_boxes) >= 2:
        items.append(path_item("telemetry-flow", f"M {right_x(telemetry_boxes[0])} {center_y([telemetry_boxes[0]])} L {left_x(telemetry_boxes[1])} {center_y([telemetry_boxes[1]])}"))

    items.append(
        box_item(
            cls="note",
            x=55,
            y=note_y,
            width=760,
            height=110,
            title="Generated layout",
            lines=[
                "This node inventory view is generated from docs/registry.md.",
                "It is representative, not a complete edge-by-edge wiring diagram.",
            ],
        )
    )
    items.append(
        box_item(
            cls="note",
            x=55,
            y=note_y + 130,
            width=CANVAS_WIDTH - 110,
            height=76,
            title="Refresh",
            lines=["Update source, sync the registry, then run make viz and make png to refresh this image."],
        )
    )

    diagram = {
        "output": "docs/visualizations/all-nodes-horizontal-stack.svg",
        "registry": str(registry_path),
        "canvas": {"width": CANVAS_WIDTH, "height": canvas_height},
        "title": "Chopper Node Stack",
        "desc": "A generated horizontal stack of every registered node, grouped by pipeline stage.",
        "items": items,
    }

    rendered = yaml.safe_dump(diagram, sort_keys=False, allow_unicode=False, width=1000)
    return "# Generated by .scripts/render_all_nodes_stack.py from docs/registry.md.\n" + rendered


def check_output(output_path: Path, rendered: str) -> int:
    if not output_path.exists():
        print(f"FAIL: generated layout missing: {output_path}", file=sys.stderr)
        return 1

    current = read_text(output_path)
    if current == rendered:
        print(f"PASS: {output_path} is up to date")
        return 0

    diff = difflib.unified_diff(
        current.splitlines(),
        rendered.splitlines(),
        fromfile=str(output_path),
        tofile=f"{output_path} (generated)",
        lineterm="",
    )
    print("\n".join(diff), file=sys.stderr)
    print(f"FAIL: {output_path} is stale; run make render-all-nodes-horizontal-stack", file=sys.stderr)
    return 1


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--registry", type=Path, default=REGISTRY, help="registry markdown file")
    parser.add_argument("--output", type=Path, default=OUTPUT, help="layout YAML output file")
    parser.add_argument("--write", action="store_true", help="write output file")
    parser.add_argument("--check", action="store_true", help="fail if output file differs")
    args = parser.parse_args()

    if args.write and args.check:
        parser.error("--write and --check are mutually exclusive")

    rendered = render_layout(args.registry)

    if args.check:
        return check_output(args.output, rendered)
    if args.write:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(rendered, encoding="utf-8")
        print(f"Wrote {args.output}")
        return 0

    print(rendered, end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
