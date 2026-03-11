# Auto-Trigger Rules (generated)

> Source: `.agents/manifest.json`. Regenerate with `python3 .scripts/render_agent_docs.py`.

Before writing to any behavioral file, compare the planned touch set against this table. If a path matches, read the listed agent file first. Skipping a required read is equivalent to skipping `make test`.

| If you plan to touch... | Read this first |
|-------------------------|-----------------|
| `**/safety/**`, `**/nodes/Motor*`, `**/nodes/Servo*`, `**/hal/IMotor*`, `**/hal/IServo*` | `.agents/personas/safety-auditor.md` -- run make check-safety before completion. |
| `**/hal/**`, `**/adapters/**` | `.agents/personas/hardware.md` |
| `main/**`, `Makefile`, `platformio.ini`, `sdkconfig.defaults`, `.scripts/**`, `docs/registry.md` | `.agents/personas/implementer.md` |
| `test/test_*.cpp`, `test/mocks/**` | `.agents/personas/tester.md` |
| `tools/telemetry_ui/**`, `description/**` | `.agents/personas/telemetry-ui.md` |
| `docs/**`, `CLAUDE.md`, `.agents/**` | `.agents/personas/docs.md` -- Path-based detection cannot auto-exempt comment-only or whitespace-only doc edits. |
| Multiple subsystems or unclear scope | `.agents/personas/orchestrator.md` |

No trigger is needed for README, `.gitignore`, CI config, or comment/formatting/whitespace/typo-only edits. If an exempt change becomes behavioral later in the task, perform the required reads before the first behavioral edit.

Use `make plan-triggers FILES="path1 path2"` for a planned touch set and `make check-triggers` for the current diff.
