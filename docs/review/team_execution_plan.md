# Team Execution Plan (Current Branch, No Git Sync)

Date: 2026-02-28  
Scope: Execute `docs/review/remediation_backlog.md` items B-001..B-011 on current local branch.  
Constraint: No `git commit`, `push`, `pull`, or history rewrite actions.

## Operating Constraints
1. Work in the current workspace branch only.
2. No git network operations.
3. Keep host tests green (`make test`) after each completed item.
4. Use feature toggles/default-off for new runtime capabilities where feasible.

## Team Topology (Spawned Squads)

### Squad A: Core Runtime
- Roles: Embedded C++ lead, parameter/config engineer, integration engineer
- Focus: B-001, B-002, B-007

### Squad B: Connectivity + Product Surface
- Roles: ESP-IDF networking engineer, API engineer, product requirements engineer
- Focus: B-003, B-004, B-005

### Squad C: Verification + Safety
- Roles: HIL validation engineer, test automation engineer, safety engineer
- Focus: B-006

### Squad D: Quality + Release Engineering
- Roles: CI/tooling engineer, static analysis engineer, release manager
- Focus: B-008, B-009, B-010, B-011

## Parallelization Model
1. Start immediately in parallel:
- Squad A: B-001 design + implementation spike
- Squad B: B-003 scope decision artifact
- Squad D: B-008 CI design draft + B-009 release checklist draft
2. Start after dependencies:
- B-002 after B-001
- B-004/B-005 after B-003 (and B-001 for parameter persistence)
- B-006 after B-001 baseline stabilizes
- B-010 after B-008 baseline pipeline exists
- B-011 after B-008 baseline pipeline exists

## Per-Item Implementation Plans

## B-001: Implement ParameterServer NVS persistence
- Owner squad: Squad A
- Goal: Replace stubs in `main/chopper/core/ParameterServer.cpp` with working persistence.
- Primary files:
  - `main/chopper/core/ParameterServer.cpp`
  - `main/include/chopper/core/ParameterServer.h` (only if API adjustments are needed)
- Plan:
  1. Add internal serialization mapping for supported param types (`int32`, `float`, `bool`).
  2. Implement `saveToNVS()` with bounded key format and per-parameter write.
  3. Implement `loadFromNVS()` with missing-key tolerance and type safety.
  4. Ensure persistence path is deterministic and non-blocking for hot path (save/load only at controlled points).
  5. Add robust error logging for NVS open/read/write failures.
- Tests to add/extend:
  - Extend config/message enhancement tests for save-load cycle behavior.
- Acceptance:
  - No stubs remain for NVS load/save.
  - Parameters survive simulated restart and retain type/range guarantees.

## B-002: Add persistence-focused test suite
- Owner squad: Squad A
- Goal: Prove B-001 behavior with deterministic tests.
- Primary files:
  - `test/test_config.cpp`
  - `test/test_message_enhancements.cpp`
  - optional test mocks under `test/mocks/`
- Plan:
  1. Add tests for initial declaration defaults.
  2. Add tests for save->reset->load restore sequence.
  3. Add tests for bad/missing keys and type mismatch fallback.
  4. Add tests for range-clamped values after reload.
- Acceptance:
  - New persistence tests pass in `make test`.
  - Negative-path behavior is explicit and stable.

## B-003: Scope decision for web/WiFi/telemetry
- Owner squad: Squad B
- Goal: Lock product scope and avoid ambiguous delivery criteria.
- Primary files:
  - `docs/review/scope_decision_record.md` (new)
  - `docs/review/requirements_traceability_matrix.md` (update)
- Plan:
  1. Document two options: implement now vs defer with versioned scope.
  2. Record chosen option with rationale, risks, and target release.
  3. Update traceability statuses to reflect approved scope.
- Acceptance:
  - Written decision with owner/date and downstream impact list.

## B-004: Minimal web parameter API (if in scope)
- Owner squad: Squad B
- Goal: Expose read/write parameter access over HTTP.
- Primary files (anticipated):
  - New module under `main/chopper/` for HTTP handlers
  - `main/main.c` initialization hooks (if needed)
  - Parameter integration with `ParameterServer`
- Plan:
  1. Implement `GET /api/params` for parameter listing.
  2. Implement `POST /api/params` for type-safe updates.
  3. Add request validation and bounded payload handling.
  4. Add default-off runtime toggle to avoid memory pressure in BT-only mode.
- Tests:
  - Host-level handler unit tests where feasible.
  - Integration smoke tests for API + parameter server interaction.
- Acceptance:
  - Read/write works for all supported types with validation.

## B-005: Telemetry output baseline (if in scope)
- Owner squad: Squad B
- Goal: Provide minimum telemetry path satisfying requirement claims.
- Primary files (anticipated):
  - New telemetry node/module under `main/chopper/`
  - hooks in existing nodes for status publication
- Plan:
  1. Define telemetry schema (actuator/sensor/system status minimal set).
  2. Implement publish path with bounded frequency.
  3. Add output routing abstraction (start with serial, optional API exposure).
  4. Make telemetry runtime-configurable (on/off and rate).
- Tests:
  - Unit tests for schema and rate limiting.
  - Integration tests for periodic emission.
- Acceptance:
  - Configurable telemetry output demonstrably active and bounded.

## B-006: Hardware-in-the-loop safety validation
- Owner squad: Squad C
- Goal: Convert safety confidence from host-only to hardware-backed evidence.
- Primary files:
  - `docs/review/hil_test_plan.md` (new)
  - `docs/review/hil_results_*.md` (new artifacts)
- Plan:
  1. Define HIL setup and instrumentation for motors/servos/audio/controller links.
  2. Execute fault scenarios:
     - controller timeout
     - loop overrun induced conditions
     - emergency stop propagation to drivers
  3. Capture pass/fail evidence and timing measurements.
- Acceptance:
  - HIL report with reproducible steps and measured outcomes.

## B-007: Refresh stale architecture audit docs
- Owner squad: Squad A
- Goal: Align audit conclusions with current implementation reality.
- Primary files:
  - `docs/design/core-framework-audit.md`
- Plan:
  1. Mark resolved findings as resolved with evidence links.
  2. Keep only active risks and open actions.
  3. Add document revision date and source snapshot note.
- Acceptance:
  - No known-resolved issue remains reported as active.

## B-008: Cross-platform CI matrix evidence
- Owner squad: Squad D
- Goal: Prove host test portability (Linux/macOS/Windows).
- Primary files (anticipated):
  - `.github/workflows/*.yml` or local equivalent CI config
  - docs evidence artifact under `docs/review/`
- Plan:
  1. Add CI jobs for each OS running host test target.
  2. Standardize compiler invocation and dependency assumptions.
  3. Publish result badges or run logs into review artifact.
- Acceptance:
  - Three-platform jobs consistently run and report status.

## B-009: Release candidate freeze process
- Owner squad: Squad D
- Goal: Ensure repeatable quality verification from clean snapshots.
- Primary files:
  - `docs/review/release_candidate_checklist.md` (new)
- Plan:
  1. Define pre-release checks (tests, docs sync, risk review, traceability review).
  2. Require clean working tree for candidate cut.
  3. Define sign-off sequence across squads.
- Acceptance:
  - Checklist exists and is used for candidate readiness reviews.

## B-010: Static analysis quality gate
- Owner squad: Squad D
- Goal: Add automated static analysis for early defect detection.
- Primary files (anticipated):
  - analysis config files
  - CI workflow updates
  - `docs/review/static_analysis_baseline.md` (new)
- Plan:
  1. Select toolchain (`clang-tidy`/`cppcheck`) and scope.
  2. Establish initial baseline; classify critical vs non-critical findings.
  3. Add fail conditions for critical categories.
- Acceptance:
  - Repeatable analysis run with documented thresholds.

## B-011: Traceability automation
- Owner squad: Squad D (+ Product engineer from Squad B)
- Goal: Keep requirement/test traceability current with minimal manual drift.
- Primary files (anticipated):
  - helper script under `scripts/` or `tools/`
  - `docs/review/requirements_traceability_matrix.md`
- Plan:
  1. Define machine-readable matrix format (table + status tags).
  2. Implement helper that flags rows missing evidence links/tests.
  3. Integrate checker into CI (warning first, then gate).
- Acceptance:
  - Automated check identifies stale/missing evidence rows.

## 2-Week Kickoff Schedule
1. Days 1-2:
- Squad A: B-001 design and persistence implementation start
- Squad B: B-003 scope decision record draft
- Squad D: B-008 CI design draft, B-009 checklist draft
2. Days 3-5:
- Squad A: B-002 tests
- Squad B: decide B-004/B-005 path after B-003 approval
- Squad D: first CI matrix run on at least one platform
3. Week 2:
- Squad C: B-006 HIL preparation and first execution cycle
- Squad A: B-007 doc refresh
- Squad D: B-010 baseline analysis; B-011 automation stub

## Exit Criteria (Program Level)
1. B-001 and B-002 complete and green.
2. B-003 approved and reflected in traceability matrix.
3. If in scope, B-004/B-005 implemented or formally deferred.
4. HIL safety evidence produced (B-006).
5. CI, release, and analysis gates established (B-008..B-011).
