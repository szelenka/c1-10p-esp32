# File Ownership (single source of truth)

> Ownership = review responsibility + default routing, not write-locks.
> In single-agent mode, write across all scopes — note which hat you're wearing.

| Path | Owner | Key rules |
|------|-------|-----------|
| `main/include/chopper/adapters/`, `main/chopper/adapters/` | hardware | Protocol specs, checksums |
| `main/include/chopper/hal/`, `main/chopper/hal/` | hardware | HAL contracts, driver registration |
| `main/include/chopper/input/` | implementer | Intent mapping (pipeline: intent mapping stage) |
| `main/include/chopper/`, `main/chopper/` (rest) | implementer | No heap, fixed arrays, safety gating |
| `test/test_*.cpp` | tester | Doctest, DEPS_ entry |
| `test/mocks/` | tester | Mirror ESP-IDF paths |
| `Makefile` | implementer | Tester advisory for DEPS_ only |
| `docs/design/` | architect | Architecture, constraints |
| `docs/` (rest) | docs | Verify against source |
| `docs/registry.md` | implementer (code tasks) / docs (sync tasks) | `make check-docs` validates |
| `tools/telemetry_ui/` | telemetry-ui | Zero-build JS, --simulate works |
| `description/` | telemetry-ui | URDF/STL |
| `.agents/` | docs | Agent config |

## Scope Exceptions

| Agent | May also write | When |
|-------|---------------|------|
| tester | `main/` (minimal fixes) | No new API, fix < test it unblocks |
| safety-auditor | inline fixes + `safety-auditor.md` | Single-agent mode, note switch |
| hardware | `test/mocks/` | Accurate HW simulation needed |

## Contested Files

| File | Default writer | Advisory |
|------|---------------|----------|
| `Makefile` DEPS_ | implementer | tester |
| `docs/registry.md` (code task) | implementer | docs |
| `docs/registry.md` (doc task) | docs | implementer |
| `joint_mapping.json` | telemetry-ui | hardware |
