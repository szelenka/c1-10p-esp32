# Triage Worked Examples

> Load on demand. Shows full reasoning chain for routing tasks.

## Example 1: "Add random movement to periscope"

1. Reaches actuators? YES — periscope uses dome Maestro servos. But the change is in the action node (PeriscopeNode), upstream of the bridge. Safety-auditor is advisory, not primary.
2. Design decision? NO — existing node, behavioral addition.
3. Multiple areas? PeriscopeNode (nodes) + test (test) + behaviors.md (docs). Single pattern.

**Route: implementer**. Tester + docs hats for test/behavior updates.
**Semantic check**: PeriscopeNode → `dome/servo_cmd` → ServoBridgeNode (safety-gated) → Maestro. Safety gate not modified. Advisory.

## Example 2: "Motor doesn't respond when I push the stick"

1. Reaches actuators? YES. Might be safety-path.
2. Scope clear? NO — could be input, mapping, node, bridge, driver, or hardware.

**Route: investigate first**. Trace the full pipeline:
```
BluepadInputNode → DriveIntentMapping → DriveNode → MotorBridgeNode → SabertoothAdapter → hardware
```
Check each stage. Common root cause: DegradationManager starts at SAFE_STOP (see `safety-auditor.md §Invariants`). If never transitioned to FULL_OPERATION, all motor commands are correctly blocked.

After root cause found: re-triage with specific fix.

## Example 3: "Add battery voltage telemetry field"

1. Reaches actuators? NO — telemetry is read-only.
2. Design decision? YES — new field crosses 4 layers.
3. Multiple areas? YES — firmware + UI + tests + docs.

**Route: orchestrator** → decompose → implementer (message + tap) → telemetry-ui (parser + display) → tester → docs.
**Golden task match**: "Add a new telemetry field" in CLAUDE.md §Task Patterns.

## Example 4: "Fix clang-tidy warning in ParameterServer.cpp"

1. Reaches actuators? NO.
2. Design decision? NO.
3. Multiple areas? NO.

**Route: express**. Single file, no behavioral change, < 20 lines. Follow CLAUDE.md §Constraints, fix, `make test && make format`.

## Example 5: "Map B button on dome controller to different sound"

1. Reaches actuators? NO — audio isn't safety-critical.
2. Multiple areas? Intent mapping + test + behaviors.md. Single pattern.

**Route: implementer**.
**Placement critical**: change goes in `DomeIntentMapping.h`, NOT `SoundNode.h`. The node handles "play sound" intents — only the button→intent mapping changes.
**Golden task match**: "Add a new button mapping" in CLAUDE.md §Task Patterns.

## Semantic Safety Trace

When path-based routing says "not safety-critical", verify:
1. List all topics your change publishes to
2. Grep subscribers: `grep -r "topic_name" main/include/chopper/`
3. If any subscriber is a bridge node → safety-critical
4. If any subscriber publishes to `*/cmd` → trace one more hop
5. If actuator reachable within 2 hops → read `safety-auditor.md`, run `make check-safety`
