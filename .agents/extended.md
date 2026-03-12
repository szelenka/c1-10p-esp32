# Chopper — Extended Reference

> Load this file only when CLAUDE.md directs you to a specific section.
> Do NOT load this file for Express or Single-Subsystem tasks.

## Safety Invariants

Canonical source. All other files reference here.

- DegradationManager starts at SAFE_STOP; requires explicit activation to FULL_OPERATION — `safety/DegradationManager.h:109`
- Executor runs safety checks every tick after node processing — `core/Executor.cpp:280` (`checkSafety`), loop at `:264`
- EmergencyStopChain: every actuator must be registered — `safety/EmergencyStopChain.h:27` (6-step chain)
- BT disconnect triggers immediate motor stop (not reduced speed) — `bluetooth/ControllerManager.h:209` → `BluepadInputNode.h:148`

### Degradation State Machine

```
FULL_OPERATION (0) → REDUCED_FEATURES (1) → ESSENTIAL_ONLY (2) → SAFE_STOP (3) → EMERGENCY_STOP (4)
                                                                       ↑ STARTS HERE
```

- Downgrade: any jump allowed. Upgrade: one step at a time. EMERGENCY_STOP requires hardware reset.
- `forceMode()`: bypasses direction check (system reset after recovery)
- Only SafetyManager may call transitions — nodes must not call `requestTransition()` directly

### Safety Checklist

Evaluate each item (pass/fail/N-A) before finishing a safety-critical task.

- [ ] Motor commands gated by DegradationManager mode
- [ ] Controller disconnect → immediate motor stop (not reduced speed)
- [ ] E-stop chain includes all actuators (motors AND servos)
- [ ] No command path bypasses SafetyManager
- [ ] Sabertooth serial TX failure detected and handled
- [ ] Servo pulse values clamped to safe ranges before transmission
- [ ] Safety checks run before motor/servo update phase in executor
- [ ] New actuators registered with EmergencyStopChain

### Safety Key Files

`main/include/chopper/safety/` — SafetyManager, DegradationManager, EmergencyStopChain, MotorSafetyMonitor
`main/include/chopper/nodes/MotorBridgeNode.h`, `ServoBridgeNode.h` — bridge nodes
`docs/design/safety-realtime-design.md` — design spec

### Safety Watch For

- Comment-only matches that make a gate appear to pass
- New `*/cmd` publishers that bypass bridge node gating
- Partial fixes that gate one actuator path but leave another path reachable

## Resource Caps

From `chopper_limits.h`:

| Resource | Limit | Constant |
|----------|-------|----------|
| Nodes | 32 | `MAX_NODES` |
| Topics | 32 | `MAX_TOPICS` |
| Subscribers/topic | 8 | `MAX_SUBSCRIBERS_PER_TOPIC` |
| Motors | 16 | `MAX_MOTORS` |
| Controllers | 4 | `MAX_CONTROLLERS` |
| Drivers | 12 | `MAX_DRIVERS` |
| Timers | 16 | `MAX_TIMERS` |
| Services | 8 | `MAX_SERVICES` |
| Parameters | 128 | `MAX_PARAMETERS` |

Sabertooth: ~4.2ms per 4-byte packet at 9600 baud. Stagger multi-motor commands.
Dual-core: Core 0 = BT/WiFi, Core 1 = application executor.

### Architect Decision Table

| Question | YES | NO |
|----------|-----|-----|
| Fits within limits? | Proceed | Reject unless user approves increase |
| New actuator path? | Safety-auditor review (BLOCKING) | Proceed |
| New message type? | User approval + check fan-out | Proceed |
| Achievable with existing primitives? | Prefer existing | Document why new abstraction needed |
| Affects executor WCET? | Timing analysis required | Proceed |

## Hardware

### Peripherals

| Device | Protocol | Constraint |
|--------|----------|------------|
| Sabertooth 2x32 | UART 9600, 4B packets | ~4.2ms/pkt, stagger |
| SyRen 10 | UART (shared/separate) | dome rotation |
| Pololu Maestro x2 | serial compact | body ch0-5, dome ch0-9, 500-2500us |
| SparkFun MP3 | serial 38400 | on-demand, <1ms TX |
| Bluepad32 | BT (Core 0) | 4 controllers, axes -512..+512 |
| OpenMV Cam | UART2 115200, binary | GPIO25 TX/GPIO33 RX, SYNC=0xA5 |

### Protocol Bytes

**Sabertooth/SyRen**: `[addr:1B][cmd:1B][data:1B][checksum:1B]` checksum = (addr+cmd+data) & 0x7F. Cmds: 0=M1fwd 1=M1rev 4=M2fwd 5=M2rev. Data: 0-127.
**Maestro**: `[0x84][ch:1B][target_lo:1B][target_hi:1B]` target = pulse_us*4. Range: 500-2500us.
**MP3 Trigger**: Play `[0x74][track:1B]` Volume `[0x76][vol:1B]` (0=max) Stop `[0x4F]`.
**OpenMV**: `[0xA5][cmd:1B][len:1B][payload:LEN][xor:1B]`. Cmds: 0x01=LED, 0x10=track, 0x80=result. Spec: `docs/design/openmv-protocol.md`.

### Hardware Key Files

`main/include/chopper/hal/` — interfaces. `main/include/chopper/adapters/` — implementations. Tests: `test/test_sabertooth.cpp`, `test_maestro.cpp`, `test_mp3trigger.cpp`.

## Orchestrator

### Sequencing

| Task | Sequence |
|------|----------|
| Bug in node | implementer → tester → (safety-auditor if actuator) |
| New node | architect (capacity) → implementer → tester → docs |
| Safety concern | safety-auditor → implementer (fix) → tester (regression) |
| HAL/driver | hardware → safety-auditor (if actuator) → tester |
| Cross-cutting | orchestrator (plan) → implementer → tester → docs |

Default: architect → implementer → safety-auditor → tester → (telemetry-ui parallel if disjoint) → reviewer → docs.

### Conflict Resolution

1. Safety always wins — BLOCKING, no override without user approval
2. Architect breaks design ties
3. User breaks scope ties — ask rather than guess
4. Never proceed past a BLOCKING finding

### Role Switching (single-agent)

Note the switch ("switching to tester hat"). Before switching: verify previous role's mechanical done-when items by running the `make` targets. For `[judgment]` items: state your assessment with evidence.

### Multi-Agent (opt-in only)

NOT the default. Requires ALL of:
1. Independent, non-overlapping file sets
2. Tool supports subagents
3. User requests it, OR 3+ independent phases
4. Merge order and final verifier are assigned up front

**Contract per parallel agent**:
- CLAUDE.md + its persona + file-set assignment + golden task (if matched)
- declare `owned_files`, `verification_run`, `verification_pending`, `depends_on`, `merge_hazards`

**Conflict**:
- agents must not edit the same writable file
- shared generated artifacts count as overlap and must be sequenced
- safety wins ties and blocks merge until resolved

**Verification**:
- run `make plan-triggers` per file set — any overlap means sequence instead
- after merge: re-run minimum verification, then re-run affected selective gates, then re-run any stale dependent checks

**Checkpoints**:
- each agent writes `.agents/handoff/CHECKPOINT-<role>.md`
- orchestrator owns stale-verification detection and final `make agent-gate-fast`

## Personas

Full done-when checklists, self-review steps, and role-specific detail. Load only when the inline Role Capsule in CLAUDE.md is insufficient.

### implementer

**Use when**: Production code changes outside docs-only, UI-only, or test-only tasks.

**Self-Review** (single-agent mode — when no dedicated reviewer is assigned):
1. Read the diff. Identify subsystems touched.
2. Trace each change through `docs/registry.md` data flow.
3. If actuator path reachable: escalate to safety-auditor (BLOCKING).
4. Confirm test coverage for behavioral changes. No assertions weakened.

**Done When**:
- [ ] `make lint-tidy` passes baseline
- [ ] `make lint-embedded` passes
- [ ] No assertions were weakened in touched tests
- [ ] If new node: test + `DEPS_` entry exist via `make check-test-inventory`
- [ ] If topics or nodes changed: `docs/registry.md` updated and `make check-docs` passes
- [ ] If servo or motor channel IDs changed: telemetry-ui follow-up was flagged
- [ ] Self-review completed (single-agent mode) or reviewer loaded (multi-agent mode)

### safety-auditor

**Use when**: Any change touches motor, servo, safety, or actuator-reachable paths — even transitively.

This robot has real motors and servos. A software bug can cause physical harm.

**Do**: In single-agent mode: fix safety issues inline, note the switch, run `make check-safety`. In multi-agent mode: emit BLOCKING or ERROR findings for implementer to fix.

**Done When**:
- [ ] Safety checklist evaluated (§Safety Checklist above) **[for each item: state pass/fail/N-A with evidence]**
- [ ] `make check-safety` and `make check-safety-ordering` pass **[run them]**
- [ ] All findings emitted with `file:line` and severity **[list them, or "no findings" with trace path]**
- [ ] If invariants changed: §Safety Invariants updated in same commit

### tester

**Use when**: Test-only work, or production changes where the main risk is regression coverage.

**Do**: Use doctest (`TEST_CASE`, `SUBCASE`, `CHECK`, `CHECK_FALSE`, `REQUIRE`). Root-cause every failure before changing behavior. Keep Makefile `DEPS_` guidance in sync — typical source groups: `$(CORE_SRCS)`, `$(SAFETY_SRCS)`, `$(DRIVER_SRC)`, `$(TIMER_SRC)`, `$(PARAM_SRC)`, `$(EXEC_SRC)`, `$(APP_SRC)`, `$(TELEMETRY_SRC)`.

**Done When**:
- [ ] `make check-test-inventory` passes
- [ ] New tests use doctest rather than legacy `TEST` / `PASS` / `ASSERT` macros
- [ ] Root cause was stated with failing assertion or code path evidence
- [ ] If new `DEPS_` group was introduced: source dependency groups stay current

### architect

**Use when**: Design decisions or capacity questions.

**Done When** (CLAUDE.md §Done items 1-8, plus):
- [ ] `make check-capacity` passes **[run it]**
- [ ] Design decision documented in `docs/design/` **[confirm rationale + alternatives recorded]**
- [ ] If actuators affected: safety-auditor consulted **[state which paths, whether reviewed]**
- [ ] If limits changed: §Resource Caps table updated

### hardware

**Use when**: HAL, driver, adapter, or serial protocol changes.

**Done When** (CLAUDE.md §Done items 1-8, plus):
- [ ] Protocol table (§Hardware above) updated if bytes changed **[diff protocol against table]**
- [ ] If channel mapping changed: flag for telemetry-ui **[grep channel constants in diff]**
- [ ] If actuator behavior changed: flag for safety-auditor **[state which actuator, whether command path affected]**

### telemetry-ui

**Use when**: UI / dashboard changes (`tools/telemetry_ui/`, `description/`).

Architecture: `ESP32 (TEL: JSON lines) → app.py (aiohttp) → WebSocket → browser (Three.js + SVG)`. Backend: Python 3, aiohttp, pyserial. Frontend: vanilla JS (zero-build), Three.js 0.160.0, urdf-loader 0.12.3.

Joint mapping: `joint_mapping.json` maps telemetry IDs → URDF joints. Sections: `servo_joints`, `dome_servo_joints`, `motor_joints`, `motor_rad_per_sec`, `led_links`. Verify: `make check-ui-mapping`.

Boundaries: MUST stay zero-build vanilla JS. MUST NOT break `python app.py --simulate`. MUST NOT hard-code serial port paths.

**Done When** (CLAUDE.md §Done items 1-8, plus):
- [ ] `make check-telemetry-sync` passes **[run it]**
- [ ] `make check-ui` and `make check-ui-mapping` pass **[run them]**
- [ ] `make test-ui-integration` passes **[run it]**
- [ ] `python app.py --simulate` works **[run it]**
- [ ] If firmware TEL format changed: coordinated with implementer **[check parser matches firmware]**
- [ ] If new servo/motor/LED: `joint_mapping.json` updated **[`make check-ui-mapping`]**

### docs

**Use when**: Documentation, registry, reference-table, or instruction-file changes.

**Do**: Treat source code and `.agents/manifest.json` as the authoritative sources. Update canonical sources before touching rendered derivatives. Use real class names, method signatures, and file references for factual claims.

**Done When**:
- [ ] API and behavior claims were checked against source or manifest with `file:line` evidence where applicable
- [ ] No code/doc or canonical/rendered inconsistencies remain
- [ ] `make check-docs` passes for registry, docs, and `.agents/` changes

### orchestrator

**Use when**: Scope is cross-cutting or unclear, 3+ personas implicated, or user requests multi-agent.

**Do**: Sequence role switching for single-agent mode. Use expanded plan format (Roles, Files per role, Merge order, Verify). Collapse back to single-agent when overlap outweighs parallelism.

**Done When**:
- [ ] All phases complete **[list each: complete / skipped-with-reason / deferred]**
- [ ] `make agent-gate-fast` passes **[run it]**
- [ ] `docs/registry.md` current **[`make check-docs`]**

### reviewer

For dedicated review phases in multi-agent pipelines only. In single-agent mode, use implementer self-review.

**Review Steps**:
1. Read the diff. Identify subsystems touched.
2. Verify trigger rules were followed.
3. Trace each change through `docs/registry.md` data flow.
4. Run embedded constraints checklist below.
5. If actuator path reachable: escalate to safety-auditor (BLOCKING).
6. Confirm test coverage for behavioral changes. No assertions weakened.

**Embedded Constraints** (beyond CLAUDE.md §Constraints):
- Resource counts within `chopper_limits.h`
- No unaccounted blocking calls in executor (serial TX ~4.2ms at 9600 baud)
- Deterministic WCET for new public functions
- const correctness (messages by const-ref)
- No unbounded loops, uninitialized memory, stack overflow risk
- Topic names match `docs/registry.md`

Severity: `[BLOCKING] > [ERROR] > [WARNING] > [NOTE]` — format: `[SEVERITY] file_path:line — description`

**Done When** (CLAUDE.md §Done items 1-8, plus):
- [ ] `make lint-tidy`, `make lint-embedded`, `make check-format` pass **[run them]**
- [ ] All findings emitted with `file:line` and severity **[list them, or "no findings" with reasoning]**

