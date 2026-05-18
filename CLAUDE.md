# Chopper — C1-10P Astromech Firmware

ESP32 C++20 embedded. ROS2-inspired pub/sub. Zero-allocation hot path.

## Vocabulary

**Intent mapping** = button/axis → semantic action tables (`DriveIntentMapping.h`, `DomeIntentMapping.h`). **Action node** = intent → device commands (`DriveNode.h`, `DomeNode.h`). **Bridge node** = safety-gated forwarding (`MotorBridgeNode.h`, `ServoBridgeNode.h`). **Area** = a route group from [Task Routing](#task-routing): safety, hardware, implementation, tests, telemetry-ui, instructions, docs.

Pipeline: `Input capture → Intent mapping → Action node → Bridge node → Driver → Hardware`

## Quick Start

Resolve top-to-bottom. Stop at the first match.

1. **Single-file edit, <20 lines, no safety impact?** → Use [.agents/express.md](.agents/express.md). Done.
2. **Touches motor/servo/safety, or can transitively reach an actuator?** → Safety-Critical mode. Also load [.agents/extended.md](.agents/extended.md) §Safety. Plan before editing.
3. **Touches 2+ areas or 3+ persona areas?** → Cross-Cutting mode. Also load [.agents/extended.md](.agents/extended.md) §Orchestrator. Plan before editing.
4. **Everything else** → Single-Subsystem mode. Use the matched [Role Capsule](#role-capsules) below, then edit.
5. **After every edit:** `make test && make format`.

## Hard Rules

These three invariants cause the most agent errors. Violating any one is a gate failure.

1. **Mapping goes in the intent table, NOT the action node.** Button-to-action logic belongs in `DriveIntentMapping.h` / `DomeIntentMapping.h`, never in `DriveNode.h` or `DomeNode.h`. Verify: `make check-placement`.
2. **NEVER weaken test assertions to make tests pass.** Removing or loosening a `CHECK`/`REQUIRE` without replacing it is always wrong. Fix the code, not the test. Verify: `make check-assertions`.
3. **GATE all motor/servo commands through DegradationManager.** No command path may bypass safety gating — even transitively through intent mapping. Verify: `make check-safety`. Safety invariants: [.agents/extended.md](.agents/extended.md) §Safety Invariants (load for safety-critical tasks).

Policy data source: `.agents/manifest.json` (consumed by scripts — agents should NOT load it).

## Policy Kernel

Use these rules to classify work by risk and behavior, not by specific files.

### Stable Invariants

1. **Stage ownership.** Raw input interpretation belongs in input capture or intent mapping. Action nodes consume semantic intent and produce commands. Bridge nodes forward commands and apply safety gating; they do not add decision logic.
2. **Safety gating.** Any path that can command a motor or servo must remain gated by DegradationManager. No direct or transitive bypass is allowed.
3. **Contract sync.** Changes to topics, nodes, message schemas, or externally observed behavior must update registry/docs in the same change.
4. **Capacity discipline.** New nodes, topics, publishers, subscribers, timers, services, or limits must stay within configured capacity.
5. **Test integrity.** Do not weaken assertions to make failures disappear. Fix code or the root cause in the test.
6. **Telemetry consistency.** If firmware telemetry contracts change, downstream parser/UI layers must stay in sync.
7. **Hot-path discipline.** Hot-path code must remain zero-allocation and avoid banned dynamic/runtime-heavy constructs.

### Verification Triggers

- **Any production code change** → `make test`, `make format`
- **Pipeline behavior or node logic changed** → `make check-placement`
- **Motor/servo/safety path may be affected** → `make check-safety`, `make check-safety-ordering`, `make check-safety-reachability`
- **New or changed command path publishes to `*/cmd`** → `make check-safety-paths`
- **Node/topic/subscriber/timer/service added** → `make check-capacity`, `make check-test-inventory`
- **Tests changed** → `make check-assertions`
- **Topics/nodes/messages/docs/registry changed** → `make check-docs`
- **Telemetry contract or UI-facing payload changed** → `make check-telemetry-sync`, `make check-ui`, `make check-ui-mapping`
- **Core execution or hot-path code changed** → `make lint-embedded`, `make lint-tidy`

## Pipeline

### Stage Ownership

| Stage | Owns | Key files |
|-------|------|-----------|
| Input capture | Raw hardware/BT data → typed messages | `BluepadInputNode.h` |
| Intent mapping | Button/axis → semantic intent (ALL mapping logic here) | `DriveIntentMapping.h`, `DomeIntentMapping.h` |
| Action node | Intent → device commands (scaling, mixing, state machines) | `DriveNode.h`, `DomeNode.h`, `PeriscopeNode.h` |
| Bridge node | Safety-gated forwarding to drivers (no decision logic) | `MotorBridgeNode.h`, `ServoBridgeNode.h` |
| Driver/adapter | Protocol bytes → wire (no logic, just I/O) | `hal/`, `adapters/` |

### Concrete Trace: L2 trigger → motor boost

```
BluepadInputNode                                        [Input capture]
  reads Bluepad32 gamepad → GamepadState{buttons, axes}
  DriveIntentMapping maps L2 trigger → boost flag        [Intent mapping]
  publishes ControllerInput to "controller/drive"

DriveNode                                                [Action node]
  subscribes "controller/drive"
  applies boost scaling → MotorCommand{left=200, right=200}
  publishes to "drive/cmd"

MotorBridgeNode                                          [Bridge node]
  subscribes "drive/cmd"
  checks DegradationManager → mode == FULL_OPERATION?
  if yes: forwards to Sabertooth driver
  if no:  drops command (safety gate)

Sabertooth driver → UART 9600 baud → motor H-bridge      [Hardware]
```

### Concrete Trace: Button remap done wrong vs. right

```
WRONG — mapping in action node:
  DriveNode.h checks `if (gamepad.buttons & BUTTON_X)` → sets boost
  ✗ Action node now contains input-layer logic
  ✗ make check-placement FAILS

RIGHT — mapping in intent table:
  DriveIntentMapping.h maps BUTTON_X → DriveIntent::BOOST
  DriveNode.h reads intent.boost flag → applies scaling
  ✓ Each stage owns only its concern
  ✓ make check-placement PASSES
```

### Placement Anti-Patterns

| Task | Wrong | Right | Why |
|------|-------|-------|-----|
| Button X → action Y | Action node checks raw button | Intent mapping table | Button-to-intent is input-layer |
| Detect combo/hold | Action node | BluepadInputNode | Needs raw input over time |
| New telemetry field | Only `app.js` | Firmware → tap → parser → UI | Each layer forwards |
| Change button action | Action node | Intent mapping | Remapping is intent-layer |
| Motor no response | Retry in action node | Trace bridge → driver → HW | Break is in delivery chain |
| Transform for display | Firmware node | `app.py` / `app.js` | Display is UI-layer |

State reasoning before editing: "This belongs in [file] because it is [stage] logic." Wrong if: action node has button bitmasks · downstream node duplicates upstream state · bridge node makes decisions beyond gating.

## Context Budget

Read at most what your task requires.

<!-- BEGIN GENERATED: context-budget -->
Startup bundle: `CLAUDE.md`, `one matched persona`, `one matching checkpoint file`.

Additional files before first edit (mode-dependent):

| Mode | Max reads before first edit |
|------|---------------------------|
| Express | 0 |
| Focused / Single-Subsystem | 3 |
| Investigate | 8 |
| Cross-Cutting / Safety-Critical | 5 |

Checkpoint triggers:
- 15 tool calls
- 5 additional file reads without editing
- context pressure

| Tier | Signal | Behavior |
|------|--------|----------|
| Express | Single file, <20 lines | Use .agents/express.md only. Skip persona and golden task. |
| Focused | Single subsystem, clear scope | Startup bundle + target file + golden task if matched. |
| Cross-Cutting | 2+ subsystems or safety-relevant | Startup bundle + one additional persona or orchestrator + plan before editing. |
<!-- END GENERATED: context-budget -->

If your effective context is <32k tokens: use [Minimum Viable Instructions](#minimum-viable-instructions) instead of full CLAUDE.md.

## Startup Algorithm

<!-- BEGIN GENERATED: startup-algorithm -->
1. Resume checkpoint if one semantically matches the task.
2. Classify the task mode and match a golden task if one fits.
3. Load the required persona or personas before the first edit.
4. Write a plan before editing if the task is cross-cutting, safety-critical, or unclear.
5. Edit only after scope, placement, and ownership are clear.
6. Run the shared minimum verification.
7. Run selective gates based on what changed.
8. Complete persona-specific done-when items.
9. Record a lesson or checkpoint if you found a gap or need to hand off.
<!-- END GENERATED: startup-algorithm -->

Plan format when required:
```
Plan: Files: [list]. Order: [sequence]. Verify: [commands].
```

For Cross-Cutting or Safety-Critical, expand: `Roles: [sequence]. Files per role: [list]. Merge order: [first]. Verify: [commands].`

## Task Modes

Use [.agents/express.md](.agents/express.md) only for true express edits. Otherwise classify first.

<!-- BEGIN GENERATED: task-modes -->
| Mode | Scope | Pre-Flight |
|------|-------|------------|
| Express | Single file, <20 lines, no safety/API/node/topic/limit change | Use .agents/express.md. Skip persona and golden task unless scope expands. |
| Investigate | Root cause unknown or could originate from multiple stages | Read docs/registry.md, trace pipeline, max 5 tool calls for scoping and 3 for confirmation. No edits. Plan required. |
| Planning-Only | Triage or decomposition with no edits | Produce sequencing and verification only. Plan required. |
| Docs-Only | docs/ or .agents/ text changes | Use docs persona. Verify cross-references and run make check-docs. |
| UI-Only | tools/telemetry_ui/ or description/ changes | Load telemetry-ui persona and run UI gates. |
| Single-Subsystem | One area, tightly coupled, clear scope | Load the matched persona and state placement reasoning before edit. |
| Safety-Critical | Motor, servo, safety, or actuator-reachable changes | Safety routing is blocking. Plan before editing. Plan required. |
| Cross-Cutting | Touches 2+ areas or 3+ personas | Load orchestrator and plan sequencing before editing. Plan required. |
<!-- END GENERATED: task-modes -->

To count areas: match your touch set against [Task Routing](#task-routing) route groups. Each matched route is one area.

## Task Patterns

Match your task against these triggers. Follow the touch set, reads, and verification.

| Pattern | Touch set | Verify |
|---------|-----------|--------|
| Button mapping / remap | `DriveIntentMapping.h`, `test_control_mapping.cpp`, `behaviors.md` | `make test`, `lint-embedded`, `check-safety-paths` |
| New node | `nodes/NewNode.h`, `test_new_node.cpp`, `Makefile` DEPS_, `registry.md` | `make test`, `lint-embedded`, `check-docs`, `check-test-inventory` |
| Safety-path change | `safety/`, bridge nodes, `test_safety.cpp` | `make test`, `check-safety`, `check-safety-ordering`, `check-safety-paths` |
| Fix failing test | `test_<name>.cpp`, possibly production code | `make test`, `lint-embedded` |
| Telemetry field | `CommonMessages.h` → `TelemetryService` → `check_telemetry_sync.py` → `app.py` → `app.js` → `registry.md` | `make test`, `check-docs`, `check-ui`, `check-telemetry-sync` |
| New peripheral | `hal/I<Name>.h`, `adapters/<Name>Adapter.h`, test, Makefile | `make test`, `lint-embedded`, `check-safety` |
| Refactor (no behavior change) | `main/include/chopper/<area>/`, test | `make test`, `lint-tidy`, `lint-embedded` |

No match? Use [Constraints](#constraints) + [Task Routing](#task-routing). Full task library (12 templates): `.agents/golden-tasks.json`.

## Task Routing

Pick the FIRST matching rule. Use the matching [Role Capsule](#role-capsules) below.

<!-- BEGIN GENERATED: task-routing -->
<!-- Generated from .agents/manifest.json. Regenerate with: make render-agent-docs -->

1. **IF** reaches motor/servo/safety (files: `**/safety/**`, `**/nodes/Motor*`, `**/nodes/Servo*`) → **safety-auditor** role. **BLOCKING.** Also load [.agents/extended.md](.agents/extended.md) §Safety.
2. **IF** design decision or capacity question → **architect** role. Also load [.agents/extended.md](.agents/extended.md) §Resource Caps.
3. **IF** touches files owned by 2+ personas → **orchestrator** role. Also load [.agents/extended.md](.agents/extended.md) §Orchestrator.
4. **IF** ui / dashboard (files: `tools/telemetry_ui/**`, `description/**`) → **telemetry-ui** role.
5. **IF** tests only (files: `test/test_*.cpp`, `test/mocks/**`) → **tester** role.
6. **IF** docs only (files: `CLAUDE.md`, `AGENTS.md`, `.agents/express.md`) → **docs** role.
7. **IF** hal / driver / adapter (files: `**/hal/**`, `**/adapters/**`) → **hardware** role. Also load [.agents/extended.md](.agents/extended.md) §Hardware.
8. **OTHERWISE** → **implementer** role.
<!-- END GENERATED: task-routing -->

### Semantic Triggers

Path rules cannot catch these.

**Safety triggers** — treat as safety-critical when:
- **Your change introduces a command that can transitively reach a motor or servo.** → Use safety-auditor role, run `make check-safety`.
- **Your change publishes to a `*/cmd` topic.** → Run `make check-safety-paths`.

**Telemetry triggers** — the telemetry UI likely needs a coordinated update when:
- **`TelemetryService.cpp/.h`** changed → Update `.scripts/check_telemetry_sync.py` schema + `app.py` parser + `app.js`. Run `make check-telemetry-sync`.
- **`CommonMessages.h` fields or new command types** → Trace through TelemetryService → parser → UI. Run `make check-telemetry-sync`.

### Multi-match

If 2 areas match: use both role capsules sequentially (safety leads) — NOT automatically Cross-Cutting. If 3+ areas: use orchestrator (Cross-Cutting mode). Safety always wins ties.

### Investigate

Root cause unknown or multi-stage. Trace using `docs/registry.md`. Max 5 tool calls for scoping, 3 for confirmation. No edits. Re-enter routing with specific scope.

### Express

Single file, <20 lines, no safety/API/node/topic change — follow [Constraints](#constraints), `make test && make format`, done. See [.agents/express.md](.agents/express.md).

## Role Capsules

Use the matched role inline. No file loading needed for single-agent work. For extended content (safety invariants, hardware protocols, orchestrator sequencing, full done-when checklists), load [.agents/extended.md](.agents/extended.md).

### implementer
- State placement reasoning before editing: "This belongs in [file] because it is [stage] logic."
- Keep hot path heap-free. Escalate to safety-auditor if change can reach an actuator.
- If telemetry format changes: run `make check-telemetry-sync`, update parser + UI.
- Watch for: high-impact files in manifest, hidden message/topic/limit changes, servo/motor channel ID changes affecting telemetry consumers.
- **Done**: `lint-tidy` baseline, `lint-embedded`, no weakened assertions, `check-test-inventory` if new node, `check-docs` if topics changed, self-review (trace data flow, check actuator reachability).

### safety-auditor
- **BLOCKING.** Load [.agents/extended.md](.agents/extended.md) §Safety for invariants and checklist.
- Trace full command flow from source topic to bridge node and registered actuator.
- Watch for: comment-only matches that make gates appear to pass, new `*/cmd` publishers bypassing bridge gating, partial fixes that gate one actuator path but leave another reachable.
- **Done**: Safety checklist evaluated (pass/fail/N-A per item), `check-safety` + `check-safety-ordering`, all findings with `file:line` and severity.

### tester
- Use doctest (`TEST_CASE`, `SUBCASE`, `CHECK`, `REQUIRE`). Never weaken assertions.
- Root-cause failures: test bug vs code bug vs stale `known_failures.txt`.
- Watch for: assertion weakening disguised as a fix, new tests without `DEPS_test_<name>` coverage, new standalone test files when an existing subsystem test should be extended.
- **Done**: `check-test-inventory`, new tests use doctest, root cause stated with evidence.

### architect
- Check resource caps in [.agents/extended.md](.agents/extended.md) §Resource Caps before adding nodes/topics/subscribers.
- New actuator path → safety-auditor review (BLOCKING). New message type → user approval.
- **Done**: `check-capacity`, design documented in `docs/design/`, safety consulted if actuators affected.

### hardware
- Protocol specs in [.agents/extended.md](.agents/extended.md) §Hardware. HAL interfaces stay hardware-agnostic.
- **Done**: Protocol table updated if bytes changed, telemetry-ui flagged if channels changed, safety-auditor flagged if actuator behavior changed.

### telemetry-ui
- Zero-build vanilla JS. Must not break `python app.py --simulate`.
- Data flow: `TEL:` → `TelemetryParser` (Python) → WebSocket → `handleTelemetry()` (JS) → `render3d.js`.
- Watch for: joint mapping out of sync with firmware channels, parser shape mismatch with live/simulated output.
- **Done**: `check-telemetry-sync`, `check-ui`, `check-ui-mapping`, `test-ui-integration`, `--simulate` works.

### docs
- Source code is authoritative. Update canonical source before rendered derivatives.
- Watch for: duplicated rules across instruction files, broken cross-references, rendered files changing without canonical source changing first.
- **Done**: Claims checked against source with `file:line` evidence, no inconsistencies, `check-docs`.

### orchestrator
- Load [.agents/extended.md](.agents/extended.md) §Orchestrator for sequencing and multi-agent protocol.
- Single-agent sequential role switching is the default. Multi-agent is opt-in only.
- **Done**: All phases complete, `agent-gate-fast`, `check-docs`.

## Constraints

Sorted by blast radius — physical damage first, cosmetic last.

### Physical damage — can break hardware

GATE all motor/servo commands through DegradationManager. No path may bypass this.
- Safety invariants: [.agents/extended.md](.agents/extended.md) §Safety Invariants
- Verify: `make check-safety` and `make check-safety-ordering`
- If your change reaches an actuator — even transitively through intent mapping — treat as safety-critical

### Correctness — breaks functionality

NEVER use on the hot path: `new`, `delete`, `malloc`, `std::string`, `std::vector`, `std::function`, `std::unordered_map`.
NEVER use RTTI: no `dynamic_cast`, `typeid` — use `TypeTag<T>::tag` pointer comparison.
RESPECT fixed-size limits in `main/include/chopper/chopper_limits.h` (caps: [.agents/extended.md](.agents/extended.md) §Resource Caps).
- Verify: `make lint-embedded`

### Code quality — causes rework

DO NOT increase clang-tidy warnings. Verify: `make lint-tidy`.
PLACE logic at the earliest correct pipeline stage. State reasoning before editing.
SOURCE OF TRUTH: source code → behavior. When docs and code disagree, code wins — flag and fix the stale doc.

### Style — cosmetic

`snake_case` functions/variables, `PascalCase` types, `ALL_CAPS` constants. `#pragma once` on all headers.
Namespaces: `chopper::core`, `chopper::hal`, `chopper::safety`, `chopper::bluetooth`, `chopper::messages`, `chopper::nodes`.
- Verify: `make format`

## Common Mistakes

| Mistake | How to avoid |
|---------|-------------|
| Button remap in action node instead of intent table | Hard Rule #1; `make check-placement`. See [wrong vs. right trace](#concrete-trace-button-remap-done-wrong-vs-right). |
| Weakened assertion to make test pass | Hard Rule #2; `make check-assertions` |
| Safety gate matched in comments, not real code | `check_safety_ordering.py` filters comments; `make check-safety` |
| Stale `known_failures.txt` entry (test now passes) | `make test` exits non-zero on stale entries |
| Logic in wrong pipeline stage | State placement reasoning; `make check-placement` |
| New actuator path introduced transitively | Trace full data flow — if actuator reachable, treat as safety-critical |
| Firmware telemetry changed but UI parser not updated | `make check-telemetry-sync`. See [Semantic Triggers](#semantic-triggers). |
| Added test to `known_failures.txt` instead of fixing | `known_failures.txt` is for pre-existing failures only |

## Verification

After every edit: `make test && make format` (seconds).
Before finishing: `make agent-gate-fast` (runs all gates, ~3 min, once).

Selective gates when `agent-gate-fast` is overkill:

<!-- BEGIN GENERATED: verification-rules -->
| What changed | Run | Skip when |
|---|---|---|
| Any code | `make test`, `make format` | `test`: another agent ran it, no code changed since. `format`: docs-only or UI-only |
| Test assertions changed | `make check-assertions` | No test files changed |
| Application.cpp / CommonMessages.h / chopper_limits.h | `make check-high-impact` (confirm with user) | No high-impact files changed |
| Node behavior or pipeline logic | `make check-placement` | Docs-only, UI-only, or test-only |
| Motor/servo/safety paths | `make check-safety`, `make check-safety-ordering`, `make check-safety-reachability` | NEVER skip for motor/servo/safety diffs |
| New node/topic/subscriber | `make check-capacity`, `make check-test-inventory` | — |
| Docs or registry | `make check-docs` | No docs/registry changes |
| Telemetry UI | `make check-ui`, `make check-ui-mapping` | Non-UI tasks |
| Telemetry format (TelemetryService, CommonMessages fields, or new command types) | `make check-telemetry-sync` | No telemetry-related firmware changes |
<!-- END GENERATED: verification-rules -->
Default: when in doubt, run it.

Gate output is structured: `GATE:<gate>:<status>:<file>:<line>:<message>`. Use `make agent-gate-json` for a JSON summary line (`GATE_SUMMARY:{...}`) at the end. Severities: `BLOCKING` (safety), `ERROR` (correctness), `WARNING` (quality). If a test fails: check `test/known_failures.txt` first. If listed, proceed. If not, investigate (max 5 tool calls). If pre-existing, add to known_failures.

## Done

Complete top-to-bottom. Do not skip.

1. `make test` passes **[run it]**
2. `make format` applied **[run it]**
3. If test assertions changed: `make check-assertions` passes **[run it]**
4. If safety-relevant: `make check-safety` and `make check-safety-reachability` pass **[run it]**
5. If docs changed: `make check-docs` passes **[run it]**
6. If high-impact files changed: `make check-high-impact` — confirm with user **[run it]**
7. Logic in correct pipeline stage: `make check-placement` passes **[run it + state: "this is [stage] logic because [reason]"]**
8. Role-specific Done items from your [Role Capsule](#role-capsules) above **[complete each]**
9. If non-Express task: `make agent-gate-fast` passes **[run it]**
10. If you discovered an instruction gap: record in `.agents/lessons/README.md`

## Resuming Previous Work

List `.agents/handoff/CHECKPOINT-*.md` files. If one matches your task and has `status: partial`, resume from its instructions. On resume, re-run verification to confirm. Stale checkpoints: ask user to delete or keep.

When context is running low (15+ tool calls, 5+ reads without editing): write a checkpoint to `.agents/handoff/CHECKPOINT-<role>.md`.

## Build Commands

Minimum: `make test && make format`. Full gate: `make agent-gate-fast`. All targets: `docs/reference/build-commands.md`.

## Key Paths

Headers: `main/include/chopper/` · Source: `main/chopper/` · Tests: `test/test_*.cpp` · Mocks: `test/mocks/`
Limits: `main/include/chopper/chopper_limits.h` · Messages: `main/include/chopper/messages/CommonMessages.h`
Registry & data flow: `docs/registry.md` · Design docs: `docs/design/` · UI: `tools/telemetry_ui/`

## File Ownership

| Path | Owner | Key rules |
|------|-------|-----------|
| `main/include/chopper/adapters/`, `adapters/` | hardware | Protocol specs |
| `main/include/chopper/hal/`, `hal/` | hardware | HAL contracts |
| `main/include/chopper/input/` | implementer | Intent mapping |
| `main/include/chopper/` (rest), `main/chopper/` | implementer | No heap, fixed arrays |
| `test/test_*.cpp`, `test/mocks/` | tester | Doctest, DEPS_ |
| `docs/design/` | architect | Architecture |
| `docs/` (rest) | docs | Verify against source |
| `tools/telemetry_ui/`, `description/` | telemetry-ui | Zero-build JS |
| `.agents/` | docs | Agent config |

In single-agent mode: write across all scopes — note which role you're using.

## Minimum Viable Instructions

For tools that cannot load files automatically or models with <32k effective context:

```
Project: ESP32 C++20 embedded astromech. Zero-allocation pub/sub hot path.
Pipeline: Input capture → Intent mapping → Action node → Bridge node → Driver → Hardware.
Vocabulary: Intent mapping = button→action tables. Action node = command logic. Bridge node = safety-gated forwarding.

HARD RULES:
1. Button mapping in intent table (DriveIntentMapping.h), NEVER in action node.
2. NEVER weaken test assertions. Fix code, not tests.
3. ALL motor/servo commands gated by DegradationManager. No bypass — even transitive.

BANNED on hot path: new, delete, malloc, std::string, std::vector, std::function, std::unordered_map, dynamic_cast, typeid.
Style: snake_case functions, PascalCase types, ALL_CAPS constants, #pragma once.

VERIFY: make test && make format (after every edit). make check-safety (if actuator path reachable).
DONE: make test, make format, placement reasoning stated, safety checked if relevant, make agent-gate-fast for non-trivial tasks.

Key paths: main/include/chopper/ (headers), main/chopper/ (source), test/test_*.cpp (tests).
```
