# Doc Sync Rules

> When you change code, update the relevant doc in the same commit.
> Enforced by: `make check-docs` (node inventory, limits, topics, generated docs).
> Persona done-when checklists also enforce these — this file is the canonical lookup table.

| When you... | Update... | Enforcement |
|-------------|-----------|-------------|
| Add/remove/rename topic, node, message type | `docs/registry.md` | gate + persona |
| Add/change agent scope | `.agents/manifest.json` + `file-conventions.md` | review + render validation |
| Change limit in `chopper_limits.h` | `architect.md` Resource Caps table | review |
| Change telemetry format (TelemetryService, CommonMessages fields) | `.scripts/check_telemetry_sync.py` schema + `app.py` parser + `app.js` handler | gate (`make check-telemetry-sync`) |
| Add/change serial protocol | `hardware.md` Protocol Bytes section | review |
| Change degradation state machine | `safety-auditor.md` (canonical source for invariants) | review + persona |
| Add build error pattern | `docs/reference/build-errors.md` | review |
| Add Makefile source dep group | `tester.md` | review |
| Discover agent error past gates | `.agents/lessons/README.md` + relevant gate | process requirement |
| Add quick decision or common mistake | `docs/reference/quick-decisions.md` or `common-mistakes.md` | review |
| Change triage routing | `docs/reference/triage-examples.md` | review |
| Change context thresholds | `.agents/manifest.json` `context_budget.thresholds` | render validation |
| Extract golden task template | `.agents/golden-tasks.json` | review |
| Lesson recurs 3+ times | Graduate to Hard Rule or `make check-*` target (see lessons/README.md §Graduation) | process requirement |
| Add node template pattern | `docs/reference/node-template.md` | review |
| Add placement anti-pattern | `.scripts/check_placement.py` BRIDGE_ALLOWED_RE or anti-pattern check | gate + review |
| Add high-impact file | `.scripts/check_high_impact.py` HIGH_IMPACT_FILES list | gate + review |
