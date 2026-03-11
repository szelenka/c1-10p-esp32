# Chopper — Project Instructions

> Instruction version: 2026-03-10

ESP32 astromech robot (C1-10P). C++20 embedded firmware with ROS2-inspired pub/sub.

These instructions are the canonical source of truth for coding agents working in this repo.
`docs/INSTRUCTIONS.md` is historical context only.

For agent workflow, task modes, and multi-agent coordination, see `.agents/policies/agent-workflow.md`.

## Quick Decisions (don't re-ask these)

- "Should I use std::string?" — No. `const char*` or fixed `char[]`.
- "Which test framework?" — Custom `TEST`/`PASS`/`ASSERT` macros. No external framework.
- "Where do new message types go?" — `CommonMessages.h`. Ask user before adding.
- "Header-only or .cpp?" — Header-only unless it has non-template out-of-line definitions or static state.
- "How to add a new node?" — Header in `main/include/chopper/nodes/`, inherit `PublishingNode`, test in `test/test_<name>.cpp`, add Makefile target + `TEST_BINS` entry.
- "How to subscribe to a topic?" — `createSubscription<MsgType>(topic, &MyNode::handler, this)` in `initialize()`. Handler signature: `void handler(const MsgType&)`.
- "How to publish?" — `auto pub = createPublisher<MsgType>(topic)` then `pub->publish(msg)`.
- "How are type IDs implemented?" — `core::getTypeId<T>()` returns a pointer to a static local. Compare by pointer equality, not RTTI.
- "What base class for nodes?" — `core::PublishingNode` if it publishes/subscribes, `core::Node` if pure logic.
- "Can I increase limits?" — Ask user first. All caps are in `chopper_limits.h`.
- "What order does Application boot?" — `addMotor()`/`addServoController()`/`addAudio()`/`addNode()` → `init()` → `start()`. Register hardware before init.
- "Where does safety gating happen?" — Bridge nodes check DegradationManager mode before forwarding commands to drivers. SafetyManager runs checks every executor tick before node processing.

## Build & Test

```bash
make test-build           # compile all host tests
make test                 # compile + run all host tests
make build                # firmware build (PlatformIO/ESP-IDF)
make lint-tidy            # clang-tidy (baseline-gated, fails if warnings increase)
make lint-tidy-changed    # clang-tidy on changed files only (fast, for iteration)
make lint-tidy-detail     # clang-tidy with full warning output
make lint-tidy-fix        # clang-tidy with auto-fix
make lint-embedded        # banned patterns check (heap types, allocations, RTTI)
make check-format         # clang-format dry-run (fails if unformatted)
make format               # clang-format apply
make analyze-cppcheck     # cppcheck static analysis
make check-safety         # verify safety gating invariants
make check-safety-ordering # semantic safety gate ordering analysis
make check-capacity       # resource usage vs chopper_limits.h (warns at 80%)
make check-docs           # detect stale docs (nodes, limits, topics)
make check-triggers       # show which agent files to read for current diff
make plan-triggers FILES="path1 path2"  # show which agent files to read for planned edits
make report-agent-gates   # structured JSON report of all gate results
make ci-local             # run the full CI gate locally
```

New test files must be wired into the Makefile and pass `make check-test-inventory`.

Additional targets (UI, multi-agent gates): see `.agents/policies/agent-workflow.md`.

## Quick Triage (route every task through this first)

```
1. Does task touch actuators (motor/servo/safety)?
   YES → safety-critical mode, read .agents/personas/safety-auditor.md
   NO  ↓
2. Does task span >1 file area (see File Conventions)?
   YES → read .agents/personas/orchestrator.md, consider parallel-subagents
   NO  ↓
3. Is scope clear and bounded?
   YES → single-subsystem mode, pick the matching agent
   NO  → human-checkpoint mode, clarify before starting

After triage: ALWAYS read the matching .agents/personas/*.md file before your first edit.
This is not optional, even in single-agent mode.
```

For multi-session or multi-agent tasks, read `.agents/policies/working-agreement.md` for handoff protocol.

## Pre-Flight (before starting any task)

1. Run `make test` — if tests fail, report to user before starting new work
2. Check `git status` — note uncommitted changes that might conflict
3. If tests are broken, do NOT assume you caused it — investigate first

Mode-specific exceptions: see `.agents/policies/agent-workflow.md`.

## Hard Rules

- **No heap on the hot path**: no `new`, `delete`, `malloc`, `std::string`, `std::vector`, `std::function`, `std::unordered_map` in pub/sub or executor code
- **No RTTI**: no `dynamic_cast`, `typeid` — use `TypeTag<T>::tag` pointer comparison
- **Fixed-size arrays only**: respect limits in `main/include/chopper/chopper_limits.h`
- **Safety system is non-negotiable**: motor/servo commands must be gated by DegradationManager; DegradationManager starts at SAFE_STOP
- **Run `make test` before finishing**: all tests must pass
- **Run `make format` before finishing**: all code must be clang-formatted
- **Do not increase clang-tidy warnings**: `make lint-tidy` is baseline-gated; if you add warnings, fix them or the gate fails
- **Safety gate**: if your change touches motor/servo/safety paths, run `make check-safety` (BLOCKING). Non-negotiable regardless of role.
- **Source of truth**: when docs and code disagree on limits, invariants, or interfaces, the source code wins; flag and fix the stale doc in the same task
- **Trigger check**: before writing to any file, consult `.agents/generated/trigger-rules.md`. Run `make check-triggers` after the first edit or before handoff.

## Common Mistakes (self-check before finishing)

- Did you use a banned type (`std::string`, `std::vector`, `std::map`, `std::function`, `new`, `malloc`)? Run `make lint-embedded`.
- Did you weaken a test assertion to make it pass? Never acceptable — fix the code, not the test.
- Did you add a subscription without checking `MAX_SUBSCRIBERS_PER_TOPIC` headroom? Run `make check-capacity`.
- Did you create a node that publishes commands to a motor/servo topic without safety gating through a bridge node?
- Did your "fix" move the safety gate to after the command dispatch? Safety checks must run before actuator output.
- Did you add a helper that allocates on the heap? Even utility functions called from nodes run on the hot path.
- Did you forget `emergencyStop()` override on a node that controls actuators?
- Did you bypass `DegradationManager` by sending commands directly to a driver instead of through the bridge node?
- Did you encounter an unfamiliar code pattern? Read the matching `docs/design/` doc before modifying. If none exists, ask the user.

## Behaviors (what the robot does, for human-to-agent translation)

Each behavior maps a controller action to a robot response. All button-triggered behaviors
go through the **intent layer** (`DriveIntentMapping.h`), so remapping a button only requires
editing the mapping table — not the node that performs the action.

**Drive controller** (`controller/drive` topic):
- Left stick → tank/arcade drive → `DriveNode` → Sabertooth motors
- Left thumb double-click → toggle carpet mode (speed boost) → `DriveNode`
- X button → raise/lower periscope → `PeriscopeNode` → dome Maestro
- A button → spin periscope left → `PeriscopeNode` → dome Maestro
- Y button → spin periscope right → `PeriscopeNode` → dome Maestro
- Select button → toggle dome doors → `DomeArmsNode` → dome Maestro
- B button (hold) → extend body utility arm → `BodyUtilityNode` → body Maestro

**Dome controller** (`controller/dome` topic):
- Left stick X → spin dome → `DomeNode` → SyRen motor
- Right stick X/Y → tilt neck (3-RSS IK) → `NeckNode` → body Maestro
- Left thumb → toggle neck enabled → `NeckNode`
- L1 / R1 → lower / raise neck height → `NeckNode`
- A button → play sound A → `SoundNode` → MP3 Trigger
- B button → play sound B → `SoundNode` → MP3 Trigger
- Start button → play random sound → `SoundNode` → MP3 Trigger

**To change what a button does**: edit the intent map in `main/include/chopper/input/DriveIntentMapping.h` (`DriveIntentMap` for drive controller, `DomeIntentMap` for dome controller).

**To change what an action does**: edit the corresponding node in `main/include/chopper/nodes/`.

**To add a new button-triggered action**: add a `UserIntent` enum value, a field on `ControllerInput`, a mapping entry, and implement the behavior in a node. Follow the "Add a node" golden task if it needs a new node.

## Style

- `snake_case` functions/variables, `PascalCase` types, `ALL_CAPS` constants
- `#pragma once` on all headers
- Namespaces: `chopper::core`, `chopper::hal`, `chopper::safety`, `chopper::bluetooth`, `chopper::messages`, `chopper::nodes`

## Agent Team

Role-specific instructions live in `.agents/personas/`. See `.agents/generated/agent-team.md` for the full routing table (generated from `.agents/manifest.json`).

In single-agent mode (the common case), you may write across all scopes. Note which "hat" you're wearing in your output. The scope restrictions in agent files become review responsibility markers, not write-locks.

Shared workflow policy lives in `.agents/policies/working-agreement.md`.

## Reference Tables (read on demand, not on every load)

These tables are extracted to keep CLAUDE.md lean. Read them when relevant:

- **Agent workflow & coordination**: `.agents/policies/agent-workflow.md`
- **Agent team routing**: `.agents/generated/agent-team.md`
- **Build errors**: `docs/reference/build-errors.md`
- **File conventions & ownership**: `.agents/policies/file-conventions.md`
- **Auto-trigger rules**: `.agents/generated/trigger-rules.md`
- **Golden task examples**: `.agents/generated/golden-tasks.md`
- **Doc sync rules**: `.agents/policies/sync-rules.md`
- **Orchestrator playbook** (examples, recipes): `.agents/policies/orchestrator-playbook.md`
- **Machine-readable repo policy**: `.agents/manifest.json`

## Topic Registry, Node Inventory, and Data Flow

Moved to `docs/registry.md`. That file is the single source of truth for topics, nodes, publishers, subscribers, and the system data flow diagram.

## Key Paths

- Headers: `main/include/chopper/`
- Implementations: `main/chopper/`
- Tests: `test/test_*.cpp`
- Mocks: `test/mocks/`
- Design docs: `docs/design/`
- Telemetry UI: `tools/telemetry_ui/`
- System limits: `main/include/chopper/chopper_limits.h`
- Message types: `main/include/chopper/messages/CommonMessages.h`
