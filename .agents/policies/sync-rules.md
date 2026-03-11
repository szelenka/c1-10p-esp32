# Keeping Docs In Sync (mandatory)

> Referenced from CLAUDE.md. This is the authoritative sync table.

When you change the codebase, update the relevant doc **in the same commit**. Stale docs are worse than no docs. Run `make check-docs` to automatically detect common staleness (node inventory, limits, topic names).

| When you... | Update... |
|-------------|-----------|
| Add/remove/rename a topic, node, or message type | `docs/registry.md` (follow `.agents/policies/working-agreement.md` registry policy) |
| Add a new agent or change agent scope | **Agent Team** table (CLAUDE.md) + `.agents/policies/file-conventions.md` |
| Change a limit in `chopper_limits.h` | `.agents/personas/architect.md` Resource Budget table |
| Add/change a serial protocol | `.agents/personas/hardware.md` Serial Protocol Quick Reference |
| Change the degradation state machine | `.agents/personas/safety-auditor.md` Degradation State Machine section |
| Add a new build error pattern | `docs/reference/build-errors.md` |
| Add a new source dependency group to Makefile | `.agents/personas/tester.md` Source Dependency Groups table |
