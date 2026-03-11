# Orchestrator Playbook

> Extended examples and parallelization recipes for multi-agent tasks.
> Read on demand -- not needed for every orchestrator decision.
> Core routing and dependency rules live in `.agents/personas/orchestrator.md`.

## Task Examples

### Example 0: "Fix off-by-one in InputMixer axis scaling" (express lane)

Single file, no safety path, <20 lines. No multi-agent ceremony needed.

| Phase | Agent | Task |
|-------|-------|------|
| 1 | implementer | Read the file, fix the bug, `make test`, commit. |

Skip: trigger reads, handoff YAML, agent file reads beyond `.agents/personas/implementer.md`, structured evidence format. See Express Lane rules in `.agents/policies/working-agreement.md`.

### Example 1: "Add periscope random movement" (new node, full pipeline)

| Phase | Agent | Task | Handoff to |
|-------|-------|------|------------|
| 1 | architect | Does random movement fit dome servo architecture? Check timing budget. | implementer |
| 2 | implementer | PeriscopeAnimationNode: subscribe to trigger, publish ServoCommand for dome:0/dome:1 | safety-auditor |
| 3 | safety-auditor | Verify: clamped ranges, degradation mode respected, e-stop halts animation | tester (via findings) |
| 4 | tester | Tests: range clamping, degradation gating, e-stop behavior | telemetry-ui |
| 5 | telemetry-ui | Verify dome:0/dome:1 render in 3D (already mapped -- just confirm) | reviewer |
| 6 | reviewer | Code review + `make analyze-cppcheck` | docs |
| 7 | docs | Update feature docs if user-facing | -- |

### Example 2: "Fix dome motor not responding to controller" (bug fix)

| Phase | Agent | Task |
|-------|-------|------|
| 1 | implementer | Trace controller/dome -> DomeNode -> dome/motor/cmd -> MotorBridgeNode. Find the break. Fix it. |
| 2 | safety-auditor | If fix touches motor path, verify safety gating still intact |
| 3 | tester | Add regression test for the specific failure mode |

### Example 3: "Add battery voltage to telemetry UI" (cross-cutting)

| Phase | Agent | Task |
|-------|-------|------|
| 1 | architect | Quick check: new SensorData message already exists? New topic needed? |
| 2 | implementer | Add BatteryMonitorNode, publish to sensor/battery, update registry |
| 3 | telemetry-ui | Add battery widget to dashboard, update TelemetryParser + joint_mapping.json |
| 4 | tester | Test BatteryMonitorNode + `make test-ui-integration` |

### Example 4: Parallel branch with join

| Phase | Agent | Task |
|-------|-------|------|
| 1 | orchestrator | Reserve `main/` for implementer and `tools/telemetry_ui/` for telemetry-ui; designate the role owning the final risky merge point as final integrator |
| 2 | implementer | Add new telemetry-producing node and update `docs/registry.md` |
| 3 | telemetry-ui | Add widget and parser support in parallel |
| 4 | tester | After both branches land, add regression coverage spanning firmware + UI contract |
| 5 | reviewer | Review integrated diff and validation evidence |
| 6 | implementer | Run final integrator gate and close task |

## Canonical Parallelization Recipes

Use these as defaults instead of inventing a new split every time.

### 1. Firmware + Tests

- `implementer`: reserves `main/`, relevant config, and `docs/registry.md`
- `tester`: waits until implementation API shape is stable, then reserves `test/` and `Makefile` test section
- `implementer` remains final integrator unless the task is test-only
- Cross-subsystem integration tests default to `tester` ownership unless the harness lives entirely in another subsystem

### 2. Firmware + Telemetry UI

- `implementer`: reserves firmware files and telemetry-producing topic changes
- `telemetry-ui`: reserves `tools/telemetry_ui/` and `description/`
- `tester`: joins after both land to validate the end-to-end contract
- Use `.agents/handoff/current_handoff.yaml` to record the shared telemetry contract and pending checks

### 3. Hardware + Safety Audit

- `hardware`: writes HAL or adapter changes
- `safety-auditor`: stays read-only and reviews the actuator path after the hardware patch is in place
- `tester`: adds regression coverage after safety findings are resolved
- Do not run hardware and safety edits in parallel on the same actuator path

### 4. Docs-Only Sync

- `docs`: owns `docs/`
- `architect`: advisory-only when design intent is unclear
- Skip full firmware retest unless the docs task uncovers a behavioral mismatch that requires code change

## End-To-End Example: Human + 2 Subagents

Scenario: a human asks for a new telemetry-producing node plus a UI widget.

1. Human creates `.agents/handoff/current_handoff.yaml` with:
   - `mode: "parallel-subagents"`
   - `primary_owner: "implementer"`
   - `final_integrator: "implementer"`
   - summary fields filled in with the requested outcome
2. Human or orchestrator runs `make plan-triggers FILES="main/include/chopper/nodes/BatteryMonitorNode.h tools/telemetry_ui/web/app.js docs/registry.md"` and reads `.agents/personas/implementer.md`, `.agents/personas/telemetry-ui.md`, and `.agents/personas/orchestrator.md`.
3. `implementer` reserves `main/` and `docs/registry.md`, ships the node and telemetry topic, then updates YAML:
   - `reservations.files`: UI files now reserved for `telemetry-ui`
   - `commands_run`: include `make test-build`
   - `human_summary.what_changed`: "BatteryMonitorNode added; publishes sensor/battery"
4. `telemetry-ui` adds the widget and mapping, updates YAML with pending checks, and records `make check-ui`.
5. `tester` takes ownership of the integration test spanning firmware and UI contract, records the final validation evidence, and hands back to the final integrator.
6. Final integrator runs `make check-handoff`, required final gates, and closes the task.
