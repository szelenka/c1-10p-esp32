# Common Mistakes

> Self-check before finishing.

| Mistake | Detection |
|---------|-----------|
| Banned type on hot path (see CLAUDE.md §Constraints) | `make lint-embedded` |
| Weakened test assertion | `make check-assertions` (net reduction); never acceptable — fix code, not test |
| Subscription exceeds MAX_SUBSCRIBERS_PER_TOPIC | `make check-capacity` |
| Published to `*/cmd` without bridge routing | `make check-safety-paths` |
| Motor/servo command bypasses safety gate | `make check-safety` |
| Safety gate after command dispatch | Must be before actuator output |
| Heap allocation in utility called from node | Hot path includes all callees |
| Missing `emergencyStop()` override on actuator node | Manual review |
| DegradationManager bypassed via direct driver call | `make check-safety-paths` |
| Logic in wrong pipeline stage | `make check-placement` + trace `docs/registry.md`; see `placement-reasoning.md` |
| Modified unfamiliar pattern without reading design doc | Check `docs/design/` first |
| New actuator path introduced transitively | Trace full path — if actuator reachable, safety-critical |
