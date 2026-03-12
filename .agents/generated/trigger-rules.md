# Auto-Trigger Rules (generated)

> Source: `.agents/manifest.json`. Regenerate with `python3 .scripts/render_agent_docs.py`.

Before writing to any behavioral file, compare the planned touch set against this table. If a path matches, read the listed agent file first. Skipping a required read is equivalent to skipping `make test`.

| If you plan to touch... | Read this first |
|-------------------------|-----------------|
| `**/safety/**`, `**/nodes/Motor*`, `**/nodes/Servo*`, `**/hal/IMotor*`, `**/hal/IServo*` | `.agents/extended.md` §safety-auditor -- BLOCKING. Run make check-safety. |
| `**/hal/**`, `**/adapters/**` | `.agents/extended.md` §hardware |
| `main/**`, `Makefile`, `platformio.ini`, `sdkconfig.defaults`, `.scripts/**`, `docs/registry.md` | `.agents/extended.md` §implementer |
| `test/test_*.cpp`, `test/mocks/**` | `.agents/extended.md` §tester |
| `tools/telemetry_ui/**`, `description/**` | `.agents/extended.md` §telemetry-ui |
| `CLAUDE.md`, `AGENTS.md`, `.agents/express.md`, `.cursorrules`, `.windsurfrules` | `.agents/extended.md` §docs -- Instruction changes have broad implications. Verify cross-references. |
| `docs/**`, `.agents/**` | `.agents/extended.md` §docs |
| `**` | `.agents/extended.md` §implementer -- Only when no other route matched. |
| Multiple subsystems or unclear scope | `.agents/extended.md` §orchestrator |

No trigger is needed for README, `.gitignore`, CI config, or comment/formatting/whitespace/typo-only edits. If an exempt change becomes behavioral later in the task, perform the required reads before the first behavioral edit.

**Multi-match rule**: 2 areas matched: load both personas, work sequentially (safety leads). Still Single-Subsystem or Safety-Critical mode, NOT automatically Cross-Cutting. 3+ areas: load orchestrator (Cross-Cutting mode). Safety (priority 0) is always BLOCKING.

Use `make plan-triggers FILES="path1 path2"` for a planned touch set and `make check-triggers` for the current diff.

## Semantic Triggers (path-based rules cannot catch these)

Path matching detects *which file* you changed, not *what your change does*. The following situations require safety-critical treatment regardless of which file is edited:

| Your change... | Trigger | Why paths miss it |
|----------------|---------|-------------------|
| Introduces a new command that reaches a motor or servo (even transitively) | Read `safety-auditor.md`, run `make check-safety` | An action node or intent map change can create a new actuator path without touching `**/safety/**` or `**/nodes/Motor*` |
| Changes `chopper_limits.h` values | Read `architect.md`, run `make check-capacity` | Limit changes affect system-wide resource budgets but don't match subsystem patterns |
| Adds or removes a topic subscriber | Run `make check-capacity` | Subscriber count changes can exceed `MAX_SUBSCRIBERS_PER_TOPIC` without touching safety or HAL files |
| Modifies intent mapping to route a button to an actuator action | Read `safety-auditor.md` | Intent map files live under `main/include/chopper/input/`, not under `**/safety/**` |
| Changes message type fields in `CommonMessages.h` | Flag for all downstream subscribers | Message type changes affect every node that subscribes to topics carrying that type |
| Publishes to a `*/cmd` topic | Run `make check-safety-paths` | New command publishers may bypass bridge node safety gating |
| Changes `TelemetryService.cpp/.h` serialization or adds/changes message fields | Run `make check-telemetry-sync`, update `app.py` parser and `app.js` | Telemetry crosses firmware→parser→UI; path rules only see the file you touched, not the downstream UI consumers |
| Adds a new motor/servo/LED/audio command type or field | Run `make check-telemetry-sync` | New command types are serialized by TelemetryService but the UI parser may not know about the new field |

**Rule of thumb**: after every change, mentally trace the data flow from your edit to all downstream consumers. If an actuator is reachable, it's safety-critical. If a capacity limit is approached, check headroom. If telemetry format changes, the UI parser must be updated.
