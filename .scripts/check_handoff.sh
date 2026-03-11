#!/usr/bin/env bash
# check_handoff.sh -- Validate .agents/handoff/current_handoff.yaml for multi-agent tasks.
# Called by: make check-handoff
set -euo pipefail

HANDOFF=".agents/handoff/current_handoff.yaml"

[ -f "$HANDOFF" ] || {
    echo "FAIL: $HANDOFF not found"
    exit 1
}

python3 - "$HANDOFF" ".agents/manifest.json" <<'PY'
import json
import sys

handoff_path = sys.argv[1]
manifest_path = sys.argv[2]

try:
    import yaml
except ImportError:
    print("FAIL: PyYAML is required for handoff validation")
    sys.exit(1)


def fail(message: str) -> None:
    print(f"FAIL: {message}")
    sys.exit(1)


def warn(message: str) -> None:
    print(f"WARNING: {message}")


def is_placeholder(value) -> bool:
    return isinstance(value, str) and "<" in value and ">" in value


with open(handoff_path, "r", encoding="utf-8") as f:
    try:
        data = yaml.safe_load(f)
    except yaml.YAMLError as exc:
        fail(f"invalid YAML in {handoff_path}: {exc}")

if not isinstance(data, dict):
    fail("handoff file must contain a top-level mapping")

with open(manifest_path, "r", encoding="utf-8") as f:
    manifest = json.load(f)

schema = manifest.get("handoff_schema")
if not isinstance(schema, dict):
    fail(".agents/manifest.json missing handoff_schema")

mode = data.get("mode")
task = data.get("task")
status = data.get("status")

# --- Sequence number validation ---
sequence = data.get("sequence")
if sequence is not None:
    if not isinstance(sequence, int) or sequence < 0:
        warn("sequence should be a non-negative integer")
elif mode == "parallel-subagents":
    warn("sequence field missing — add a monotonic counter to detect concurrent update conflicts")

# --- Mode validation ---
if not isinstance(mode, str) or not mode.strip():
    fail("mode missing or not a string")
if not isinstance(task, str) or not task.strip():
    fail("task missing or not a string")

allowed_modes = set(schema.get("allowed_modes", []))
if mode not in allowed_modes:
    fail(f"mode must be one of {sorted(allowed_modes)}")

# --- Status validation ---
allowed_statuses = set(schema.get("allowed_statuses", []))
if isinstance(status, str) and status not in allowed_statuses:
    fail(f"status must be one of {sorted(allowed_statuses)}, got '{status}'")

# --- Safety impact validation ---
safety_impact = data.get("safety_impact")
allowed_safety = set(schema.get("allowed_safety_impacts", []))
if isinstance(safety_impact, str) and safety_impact not in allowed_safety:
    fail(f"safety_impact must be one of {sorted(allowed_safety)}, got '{safety_impact}'")

# --- Safety-check enforcement ---
if safety_impact == "actuator":
    cmds_run = data.get("commands_run", [])
    cmds_required = data.get("commands_still_required", [])
    all_cmds = cmds_run + cmds_required if isinstance(cmds_run, list) and isinstance(cmds_required, list) else []
    safety_mentioned = any("check-safety" in str(c) for c in all_cmds)
    if not safety_mentioned:
        fail("safety_impact is 'actuator' but 'make check-safety' not found in commands_run or commands_still_required")

# --- Non-draft must have human_summary ---
if isinstance(status, str) and status != "draft":
    human_summary = data.get("human_summary")
    if isinstance(human_summary, dict):
        for field in ("what_changed", "why"):
            value = human_summary.get(field)
            if not isinstance(value, str) or not value.strip() or is_placeholder(value):
                warn(f"human_summary.{field} is empty or placeholder for status='{status}'")

# --- Parallel-subagents strict checks ---
if mode == "parallel-subagents":
    for field in schema.get("parallel_required_string_fields", []):
        value = data.get(field)
        if not isinstance(value, str) or not value.strip():
            fail(f"{field} missing or not a string")
        if is_placeholder(value):
            fail(f"{field} still uses placeholder value")

    reservations = data.get("reservations")
    if not isinstance(reservations, dict):
        fail("reservations missing or not a mapping")
    for field in schema.get("parallel_required_reservation_lists", []):
        value = reservations.get(field)
        if not isinstance(value, list):
            fail(f"reservations.{field} missing or not a list")

    human_summary = data.get("human_summary")
    if not isinstance(human_summary, dict):
        fail("human_summary missing or not a mapping")
    for field in schema.get("parallel_required_human_summary_fields", []):
        value = human_summary.get(field)
        if not isinstance(value, str) or not value.strip():
            fail(f"human_summary.{field} missing or not a string")
        if is_placeholder(value):
            fail(f"human_summary.{field} still uses placeholder value")

    read_first = human_summary.get("read_first")
    if not isinstance(read_first, list) or not read_first:
        fail("human_summary.read_first missing or not a non-empty list")
    if any(not isinstance(item, str) or not item.strip() or is_placeholder(item) for item in read_first):
        fail("human_summary.read_first must contain concrete file paths")

    for field in schema.get("parallel_required_list_fields", []):
        value = data.get(field)
        if not isinstance(value, list):
            fail(f"{field} missing or not a list")

    print("PASS: multi-agent handoff artifact populated")
else:
    print(f"PASS: handoff artifact present; strict population not required for mode={mode}")
PY
