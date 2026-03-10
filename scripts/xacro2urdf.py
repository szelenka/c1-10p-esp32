#!/usr/bin/env python3
"""Resolve a fusion2urdf xacro export into a plain URDF for the web viewer.

Handles the simple case where xacro:include files contain only <material>,
<transmission>, and <gazebo> elements (none of which the web viewer needs).
The material definitions are inlined from materials.xacro; transmissions and
gazebo tags are dropped.

Usage:
    python scripts/xacro2urdf.py <export_dir> <output.urdf> [--robot-name NAME]

If the `xacro` CLI is available it will be used instead (full fidelity).
"""
import argparse
import re
import shutil
import subprocess
import sys
import xml.etree.ElementTree as ET
from pathlib import Path


def try_xacro_cli(xacro_path: Path, output_path: Path) -> bool:
    """Attempt to use the ROS xacro CLI.  Returns True on success."""
    if not shutil.which("xacro"):
        return False
    try:
        result = subprocess.run(
            ["xacro", str(xacro_path)],
            capture_output=True, text=True, timeout=30,
        )
        if result.returncode == 0 and result.stdout.strip():
            output_path.write_text(result.stdout)
            return True
        print(f"xacro CLI failed (rc={result.returncode}): {result.stderr.strip()}", file=sys.stderr)
    except Exception as exc:
        print(f"xacro CLI error: {exc}", file=sys.stderr)
    return False


def resolve_manually(xacro_path: Path, output_path: Path, robot_name: str) -> None:
    """Strip xacro directives and inline materials to produce plain URDF."""
    text = xacro_path.read_text()

    # Remove xacro namespace declaration
    text = re.sub(r'\s+xmlns:xacro="[^"]*"', "", text)

    # Remove xacro:include lines
    text = re.sub(r"^\s*<xacro:include[^>]*/>\s*\n?", "", text, flags=re.MULTILINE)

    # Remove xacro:property lines
    text = re.sub(r"^\s*<xacro:property[^>]*/>\s*\n?", "", text, flags=re.MULTILINE)

    # Rename robot if requested
    if robot_name:
        text = re.sub(r'(<robot\s+name=")[^"]*(")', rf"\g<1>{robot_name}\g<2>", text)

    # Parse and re-serialize to validate XML
    try:
        tree = ET.ElementTree(ET.fromstring(text))
    except ET.ParseError as exc:
        print(f"Warning: XML parse error after stripping xacro: {exc}", file=sys.stderr)
        print("Writing raw output anyway.", file=sys.stderr)
        output_path.write_text(text)
        return

    # Inline material definitions from materials.xacro if not already present
    root = tree.getroot()
    materials_file = xacro_path.parent / "materials.xacro"
    if materials_file.is_file():
        try:
            mat_text = materials_file.read_text()
            mat_text = re.sub(r'\s+xmlns:xacro="[^"]*"', "", mat_text)
            mat_root = ET.fromstring(mat_text)
            existing = {m.get("name") for m in root.findall("material")}
            for mat in mat_root.findall("material"):
                if mat.get("name") not in existing:
                    # Insert after xml declaration, before first link
                    root.insert(0, mat)
        except Exception as exc:
            print(f"Warning: could not inline materials: {exc}", file=sys.stderr)

    # Write with xml declaration and comment
    ET.indent(tree, space="  ")
    header = '<?xml version="1.0" ?>\n'
    header += "<!--\n"
    header += f"  Resolved from {xacro_path.name} by xacro2urdf.py\n"
    header += "  Transmissions and Gazebo tags stripped for web viewer.\n"
    header += "-->\n"
    output_path.write_text(header + ET.tostring(root, encoding="unicode"))


def main() -> None:
    parser = argparse.ArgumentParser(description="Convert fusion2urdf xacro export to plain URDF")
    parser.add_argument("export_dir", type=Path, help="Path to fusion2urdf_description directory")
    parser.add_argument("output", type=Path, help="Output URDF file path")
    parser.add_argument("--robot-name", default="chopper_fusion", help="Robot name in URDF (default: chopper_fusion)")
    args = parser.parse_args()

    export_dir = args.export_dir.expanduser().resolve()
    urdf_dir = export_dir / "urdf"

    # Find the main xacro file
    xacro_files = list(urdf_dir.glob("*.xacro"))
    main_xacro = None
    for f in xacro_files:
        if f.name not in ("materials.xacro",):
            # The main xacro is the one that includes others
            content = f.read_text()
            if "xacro:include" in content:
                main_xacro = f
                break
    if main_xacro is None:
        # Fallback: look for any .xacro that isn't materials
        candidates = [f for f in xacro_files if f.name != "materials.xacro"]
        if candidates:
            main_xacro = candidates[0]
        else:
            print(f"Error: no xacro files found in {urdf_dir}", file=sys.stderr)
            sys.exit(1)

    print(f"Source xacro: {main_xacro}")
    print(f"Output URDF:  {args.output}")

    # Try xacro CLI first
    if try_xacro_cli(main_xacro, args.output):
        print("Resolved via xacro CLI")
    else:
        print("xacro CLI not available, resolving manually (stripping includes)")
        resolve_manually(main_xacro, args.output, args.robot_name)

    print("Done.")


if __name__ == "__main__":
    main()
