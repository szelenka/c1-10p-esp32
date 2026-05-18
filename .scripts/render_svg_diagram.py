#!/usr/bin/env python3
"""Render checked SVG diagrams from YAML specs.

The renderer is intentionally small and deterministic. Diagram-specific
coordinates live in docs/visualizations/*.layout.yml; this script provides
shared SVG primitives plus registry validation so diagrams do not silently
drift away from docs/registry.md.
"""

from __future__ import annotations

import argparse
import difflib
import html
import sys
from pathlib import Path
from typing import Any

import yaml


REGISTRY = Path("docs/registry.md")

STYLE = """\
      .bg { fill: #f7f9fb; }
      .title { font: 700 34px Arial, sans-serif; fill: #101828; }
      .subtitle { font: 18px Arial, sans-serif; fill: #475467; }
      .band-title { font: 700 18px Arial, sans-serif; fill: #344054; letter-spacing: 0; }
      .box-title { font: 700 18px Arial, sans-serif; fill: #101828; letter-spacing: 0; }
      .box-text { font: 15px Arial, sans-serif; fill: #344054; letter-spacing: 0; }
      .small { font: 13px Arial, sans-serif; fill: #475467; letter-spacing: 0; }
      .topic { fill: #e0f2fe; stroke: #0284c7; stroke-width: 2; }
      .input { fill: #ecfdf3; stroke: #039855; stroke-width: 2; }
      .action { fill: #fff7ed; stroke: #f97316; stroke-width: 2; }
      .bridge { fill: #fef3f2; stroke: #d92d20; stroke-width: 2; }
      .hardware { fill: #eef4ff; stroke: #444ce7; stroke-width: 2; }
      .telemetry { fill: #f4f3ff; stroke: #7a5af8; stroke-width: 2; }
      .note { fill: #ffffff; stroke: #d0d5dd; stroke-width: 1.5; }
      .line { stroke: #344054; stroke-width: 3; fill: none; marker-end: url(#arrow); stroke-linecap: round; stroke-linejoin: round; }
      .red-line { stroke: #b42318; stroke-width: 3; fill: none; marker-end: url(#redArrow); stroke-linecap: round; stroke-linejoin: round; }
      .drive-line { stroke: #027a48; stroke-width: 3; fill: none; marker-end: url(#driveArrow); stroke-linecap: round; stroke-linejoin: round; }
      .dome-line { stroke: #6941c6; stroke-width: 3; fill: none; marker-end: url(#domeArrow); stroke-linecap: round; stroke-linejoin: round; }
      .control-bus { stroke: #344054; stroke-width: 4; fill: none; stroke-linecap: round; stroke-linejoin: round; }
      .drive-bus { stroke: #027a48; stroke-width: 5; fill: none; stroke-linecap: round; stroke-linejoin: round; }
      .dome-bus { stroke: #6941c6; stroke-width: 5; fill: none; stroke-linecap: round; stroke-linejoin: round; }
      .line-shield { stroke: #f7f9fb; stroke-width: 9; fill: none; stroke-linecap: round; }
      .control-junction { fill: #344054; }
      .drive-junction { fill: #027a48; }
      .dome-junction { fill: #6941c6; }
      .telemetry-line { stroke: #7a5af8; stroke-width: 2.5; fill: none; stroke-linecap: round; stroke-linejoin: round; stroke-dasharray: 7 6; }
      .telemetry-flow { stroke: #7a5af8; stroke-width: 2.5; fill: none; stroke-linecap: round; stroke-linejoin: round; stroke-dasharray: 7 6; marker-end: url(#telemetryArrow); }
      .telemetry-junction { fill: #7a5af8; }
      .joycon-body { fill: #f2f4f7; stroke: #667085; stroke-width: 2.5; }
      .joycon-rail { fill: #d0d5dd; stroke: #667085; stroke-width: 1.5; }
      .joycon-button { fill: #ffffff; stroke: #667085; stroke-width: 1.5; }
      .joycon-stick-well { fill: #ffffff; stroke: #667085; stroke-width: 2; }
      .joycon-stick-dot { fill: #344054; stroke: #101828; stroke-width: 1.5; }
      .joycon-input { fill: #fdb022; stroke: #b54708; stroke-width: 2.5; }
      .joycon-input-ghost { fill: none; stroke: #fdb022; stroke-width: 2.5; stroke-dasharray: 5 4; }
      .joycon-label { font: 700 7px Arial, sans-serif; fill: #344054; text-anchor: middle; dominant-baseline: middle; letter-spacing: 0; }
      .input-callout { fill: #f2f4f7; stroke: #98a2b3; stroke-width: 1.5; }
      .thin { stroke-width: 2; }
      .dash { stroke-dasharray: 8 7; }"""


def esc(value: Any) -> str:
    return html.escape(str(value), quote=True)


def attrs(values: dict[str, Any]) -> str:
    rendered = []
    for key, value in values.items():
        if value is None:
            continue
        rendered.append(f'{key.replace("_", "-")}="{esc(value)}"')
    return " ".join(rendered)


def load_yaml(path: Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as fh:
        data = yaml.safe_load(fh)
    if not isinstance(data, dict):
        raise ValueError(f"{path} must contain a YAML mapping")
    return data


def parse_registry(path: Path) -> tuple[set[str], set[str]]:
    topics: set[str] = set()
    nodes: set[str] = set()
    in_topic_table = False
    in_node_table = False

    for line in path.read_text(encoding="utf-8").splitlines():
        if line.startswith("| Topic"):
            in_topic_table = True
            in_node_table = False
            continue
        if line.startswith("| Node") and "Header" in line:
            in_node_table = True
            in_topic_table = False
            continue
        if line.startswith("|---"):
            continue
        if not line.startswith("|"):
            in_topic_table = False
            in_node_table = False
            continue

        cols = [c.strip() for c in line.split("|")]
        if in_topic_table and len(cols) >= 5:
            topics.add(cols[1].strip("`"))
        elif in_node_table and len(cols) >= 5:
            nodes.add(cols[1])

    return topics, nodes


def collect_refs(value: Any) -> tuple[set[str], set[str]]:
    topics: set[str] = set()
    nodes: set[str] = set()

    if isinstance(value, dict):
        refs = value.get("refs")
        if isinstance(refs, dict):
            topics.update(str(t) for t in refs.get("topics", []) or [])
            nodes.update(str(n) for n in refs.get("nodes", []) or [])
        for child in value.values():
            child_topics, child_nodes = collect_refs(child)
            topics.update(child_topics)
            nodes.update(child_nodes)
    elif isinstance(value, list):
        for child in value:
            child_topics, child_nodes = collect_refs(child)
            topics.update(child_topics)
            nodes.update(child_nodes)

    return topics, nodes


def validate_registry(diagram: dict[str, Any], spec_path: Path) -> None:
    registry_path = Path(diagram.get("registry", REGISTRY))
    if not registry_path.exists():
        raise FileNotFoundError(f"registry file not found: {registry_path}")

    registry_topics, registry_nodes = parse_registry(registry_path)
    ref_topics, ref_nodes = collect_refs(diagram)

    missing_topics = sorted(ref_topics - registry_topics)
    missing_nodes = sorted(ref_nodes - registry_nodes)
    if missing_topics or missing_nodes:
        details = []
        if missing_topics:
            details.append(f"topics not in {registry_path}: {', '.join(missing_topics)}")
        if missing_nodes:
            details.append(f"nodes not in {registry_path}: {', '.join(missing_nodes)}")
        raise ValueError(f"{spec_path} references stale registry entries: {'; '.join(details)}")


def render_defs() -> list[str]:
    return [
        "  <defs>",
        '    <marker id="arrow" viewBox="0 0 10 10" refX="8" refY="5" markerWidth="9" markerHeight="9" orient="auto-start-reverse">',
        '      <path d="M 0 0 L 10 5 L 0 10 z" fill="#344054"/>',
        "    </marker>",
        '    <marker id="redArrow" viewBox="0 0 10 10" refX="8" refY="5" markerWidth="9" markerHeight="9" orient="auto-start-reverse">',
        '      <path d="M 0 0 L 10 5 L 0 10 z" fill="#b42318"/>',
        "    </marker>",
        '    <marker id="driveArrow" viewBox="0 0 10 10" refX="8" refY="5" markerWidth="9" markerHeight="9" orient="auto-start-reverse">',
        '      <path d="M 0 0 L 10 5 L 0 10 z" fill="#027a48"/>',
        "    </marker>",
        '    <marker id="domeArrow" viewBox="0 0 10 10" refX="8" refY="5" markerWidth="9" markerHeight="9" orient="auto-start-reverse">',
        '      <path d="M 0 0 L 10 5 L 0 10 z" fill="#6941c6"/>',
        "    </marker>",
        '    <marker id="telemetryArrow" viewBox="0 0 10 10" refX="8" refY="5" markerWidth="8" markerHeight="8" orient="auto-start-reverse">',
        '      <path d="M 0 0 L 10 5 L 0 10 z" fill="#7a5af8"/>',
        "    </marker>",
        "    <style>",
        STYLE,
        "    </style>",
        "  </defs>",
    ]


def render_text(item: dict[str, Any]) -> str:
    return f'  <text {attrs({"class": item.get("class", "box-text"), "x": item["x"], "y": item["y"], "text_anchor": item.get("text_anchor"), "transform": item.get("transform")})}>{esc(item["text"])}</text>'


def render_box(item: dict[str, Any]) -> list[str]:
    lines = [
        f'  <rect {attrs({"class": item["class"], "x": item["x"], "y": item["y"], "width": item["width"], "height": item["height"], "rx": item.get("rx", 8)})}/>',
    ]
    if "title" in item:
        title = item["title"]
        lines.append(render_text({"class": "box-title", **title}))
    for line in item.get("lines", []) or []:
        if isinstance(line, str):
            raise ValueError("box line entries must include x/y coordinates")
        lines.append(render_text({"class": line.get("class", "box-text"), **line}))
    return lines


def render_path(item: dict[str, Any]) -> str:
    return f'  <path {attrs({"class": item.get("class"), "d": item["d"], "fill": item.get("fill"), "stroke": item.get("stroke"), "stroke_width": item.get("stroke_width"), "transform": item.get("transform")})}/>'


def render_circle(item: dict[str, Any]) -> str:
    return f'  <circle {attrs({"class": item.get("class"), "cx": item["cx"], "cy": item["cy"], "r": item["r"], "fill": item.get("fill"), "stroke": item.get("stroke"), "stroke_width": item.get("stroke_width"), "transform": item.get("transform")})}/>'


def render_item(item: dict[str, Any]) -> list[str]:
    kind = item["kind"]
    if kind == "text":
        return [render_text(item)]
    if kind == "box":
        return render_box(item)
    if kind == "path":
        return [render_path(item)]
    if kind == "circle":
        return [render_circle(item)]
    raise ValueError(f"unknown diagram item kind: {kind}")


def render_svg(diagram: dict[str, Any], spec_path: Path) -> str:
    canvas = diagram["canvas"]
    width = canvas["width"]
    height = canvas["height"]
    title = diagram["title"]
    desc = diagram["desc"]

    lines = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}" role="img" aria-labelledby="title desc">',
        f'  <title id="title">{esc(title)}</title>',
        f'  <desc id="desc">{esc(desc)}</desc>',
        f"  <!-- Generated by .scripts/render_svg_diagram.py from {spec_path}. -->",
    ]
    lines.extend(render_defs())
    lines.append("")
    lines.append(f'  <rect class="bg" x="0" y="0" width="{width}" height="{height}"/>')
    for item in diagram["items"]:
        lines.extend(render_item(item))
    lines.append("</svg>")
    return "\n".join(lines) + "\n"


def check_output(output_path: Path, rendered: str) -> int:
    if not output_path.exists():
        print(f"FAIL: generated output missing: {output_path}", file=sys.stderr)
        return 1

    current = output_path.read_text(encoding="utf-8")
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
    print(f"FAIL: {output_path} is stale; run make viz", file=sys.stderr)
    return 1


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("spec", type=Path, help="YAML diagram spec")
    parser.add_argument("--write", action="store_true", help="write output file")
    parser.add_argument("--check", action="store_true", help="fail if output file differs")
    args = parser.parse_args()

    if args.write and args.check:
        parser.error("--write and --check are mutually exclusive")

    diagram = load_yaml(args.spec)
    validate_registry(diagram, args.spec)
    rendered = render_svg(diagram, args.spec)
    output_path = Path(diagram["output"])

    if args.check:
        return check_output(output_path, rendered)
    if args.write:
        output_path.parent.mkdir(parents=True, exist_ok=True)
        output_path.write_text(rendered, encoding="utf-8")
        print(f"Wrote {output_path}")
        return 0

    print(rendered, end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
