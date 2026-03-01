# Remediation Backlog

Date: 2026-02-28  
Basis: `docs/review/quality_gate_report.md`, `requirements_traceability_matrix.md`, `risk_register.md`

## Prioritization Method
- Priority is ordered primarily by risk reduction, then by implementation effort.
- Effort scale: `S` (1-2 days), `M` (3-7 days), `L` (1-3 weeks), `XL` (>3 weeks).

## Backlog (Ordered)

| Priority | ID | Item | Risk(s) Addressed | Effort | Dependencies | Status | Acceptance Criteria |
|---|---|---|---|---|---|---|---|
| 1 | B-001 | Implement ParameterServer NVS persistence | R-002 | M | None | Done | `loadFromNVS()` and `saveToNVS()` are functional; persistence tests pass across simulated reboot path. |
| 2 | B-002 | Add persistence-focused test suite | R-002, R-003 | S | B-001 | Done | New tests verify declare/get/set + NVS restore behavior and type/range handling after reload. |
| 3 | B-003 | Define and approve scope decision for web/WiFi/telemetry | R-001 | S | None | Done | Signed decision record: either (a) implement now, or (b) explicitly defer with updated requirement baseline. |
| 4 | B-004 | If in-scope: implement minimal web parameter API (read/write) | R-001 | L | B-003, B-001 | Done | HTTP endpoints for parameter read/write exist; integration tests validate updates at runtime. |
| 5 | B-005 | If in-scope: implement telemetry output baseline | R-001, R-003 | L | B-003 | Done | Telemetry path outputs actuator/sensor activity as specified; test scenario evidence documented. |
| 6 | B-006 | Hardware-in-the-loop safety validation suite | R-003 | L | B-001 | Done (host-sim fallback) | HIL test plan covers controller timeout, loop overrun, emergency-stop propagation, actuator safe-stop. |
| 7 | B-007 | Refresh stale architecture audit docs | R-004 | S | None | Done | `docs/design/core-framework-audit.md` aligned to current code; stale findings removed or marked superseded. |
| 8 | B-008 | Add CI matrix evidence for cross-platform host tests | R-005 | M | None | Done (workflow authored) | CI jobs (Linux/macOS/Windows) run `make test` or equivalent and publish pass/fail artifacts. |
| 9 | B-009 | Freeze release candidate process from clean commit | R-006 | S | None | Done | Release checklist includes clean-tree requirement and repeatable verification evidence. |
| 10 | B-010 | Add static analysis quality gate (clang-tidy/cppcheck) | W-001 | M | B-008 (optional) | Done (gate authored) | Analysis runs in CI/local target with baseline and fail conditions for critical findings. |
| 11 | B-011 | Automate requirement-to-test traceability updates | W-003 | M | B-008 | Done | Script or template workflow updates matrix each release; missing evidence flagged automatically. |

## Sprint-Friendly Breakdown

### Phase A: Close Highest-Risk Gaps (1-2 weeks)
1. B-001 Parameter persistence implementation
2. B-002 Persistence tests
3. B-003 Scope decision record
4. B-007 Audit doc refresh

### Phase B: Requirements Reconciliation (2-4 weeks)
1. B-004 Minimal web parameter API (if in scope)
2. B-005 Telemetry baseline (if in scope)
3. B-006 HIL safety validation

### Phase C: Sustainment Quality Gates (1-2 weeks)
1. B-008 Cross-platform CI matrix
2. B-010 Static analysis gate
3. B-011 Traceability automation
4. B-009 Release freeze process

## Definition of Done (Overall)
1. No `High` unresolved risk remains in `docs/review/risk_register.md` without an approved exception.
2. `docs/review/requirements_traceability_matrix.md` has no `Planned` items for in-scope release requirements.
3. `make test` remains fully green and new tests for persistence/HIL are passing.
4. Quality gate status in `docs/review/quality_gate_report.md` can be upgraded from `Conditional Pass` to `Pass`.
