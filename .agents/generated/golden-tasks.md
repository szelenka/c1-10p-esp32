# Golden Tasks (generated)

> Source: `.agents/manifest.json`. Regenerate with `python3 .scripts/render_agent_docs.py`.

These are compact, canonical task shapes for AI agents. Treat them as starting patterns, not rigid templates.

## Add an existing-pattern node

Touch set:
- `main/include/chopper/nodes/NewNode.h`
- `test/test_new_node.cpp`
- `Makefile`
- `docs/registry.md`

Read first:
- `CLAUDE.md`
- `.agents/personas/implementer.md`
- `.agents/personas/tester.md`
- `.agents/personas/docs.md`
- `.agents/personas/orchestrator.md`

Verify with:
- `make test-build`
- `make test`
- `make lint-embedded`
- `make check-docs`

Done when:
- Node follows PublishingNode pattern.
- A host test target and TEST_BINS entry exist.
- docs/registry.md reflects the new node/topics.

## Safety-path behavior change

Touch set:
- `main/include/chopper/nodes/MotorBridgeNode.h`
- `main/chopper/safety/DegradationManager.cpp`
- `test/test_safety.cpp`

Read first:
- `CLAUDE.md`
- `.agents/personas/implementer.md`
- `.agents/personas/safety-auditor.md`
- `.agents/personas/tester.md`
- `.agents/personas/orchestrator.md`

Verify with:
- `make test`
- `make check-safety`
- `make lint-embedded`
- `make check-docs`

Done when:
- Actuator commands remain gated by degradation mode.
- Regression coverage exists for the changed safety path.
- No BLOCKING safety findings remain.

## Telemetry UI parser or schema update

Touch set:
- `tools/telemetry_ui/app.py`
- `tools/telemetry_ui/web/app.js`
- `.scripts/test_ui_integration.py`

Read first:
- `CLAUDE.md`
- `.agents/personas/telemetry-ui.md`
- `.agents/personas/tester.md`
- `.agents/personas/orchestrator.md`

Verify with:
- `make check-ui`
- `make test-ui-integration`

Done when:
- The parser accepts the new frame shape.
- The UI syntax gate passes.
- Integration coverage reflects the updated schema.

## Fix a failing test

Touch set:
- `test/test_<name>.cpp`
- `main/include/chopper/<area>/<File>.h`

Read first:
- `CLAUDE.md`
- `.agents/personas/tester.md`
- `.agents/personas/implementer.md`

Verify with:
- `make test`
- `make lint-embedded`

Done when:
- Root cause identified (test bug vs code bug).
- Fix applied to the correct side (test or production code).
- No test assertions weakened to make the test pass.
- All other tests still pass.

## Docs-only reference cleanup

Touch set:
- `CLAUDE.md`
- `.agents/generated/trigger-rules.md`
- `.agents/generated/golden-tasks.md`

Read first:
- `CLAUDE.md`
- `.agents/personas/docs.md`

Verify with:
- `make check-docs`

Done when:
- Canonical docs point to one source of truth.
- Historical docs are labeled as such.
- Generated reference docs are refreshed.
