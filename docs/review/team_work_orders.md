# Team Work Orders

Date: 2026-02-28  
Constraint: Execute on current branch; no git commit/push/pull actions.

## Work Order WO-A (Squad A: Core Runtime)
- Items: B-001, B-002, B-007
- Start condition: immediate
- Deliverables:
  1. Parameter persistence implementation and tests
  2. Updated core architecture audit alignment
- Daily checkpoint:
  1. `make test` status
  2. Open blockers
  3. Files touched

## Work Order WO-B (Squad B: Connectivity + Product Surface)
- Items: B-003, B-004, B-005
- Start condition:
  1. B-003 immediate
  2. B-004/B-005 after scope decision approval
- Deliverables:
  1. Scope decision record
  2. Optional minimal web API + telemetry baseline (if approved in scope)
- Daily checkpoint:
  1. Scope status
  2. API/telemetry design decisions
  3. Test evidence added

## Work Order WO-C (Squad C: Verification + Safety)
- Items: B-006
- Start condition: after B-001 baseline stabilizes
- Deliverables:
  1. HIL test plan
  2. First HIL results package
- Daily checkpoint:
  1. Test bench readiness
  2. Scenario execution status
  3. Safety finding severity

## Work Order WO-D (Squad D: Quality + Release)
- Items: B-008, B-009, B-010, B-011
- Start condition:
  1. B-008/B-009 immediate
  2. B-010/B-011 after basic CI flow lands
- Deliverables:
  1. Cross-platform CI evidence
  2. Release candidate checklist
  3. Static analysis baseline + gate proposal
  4. Traceability automation helper
- Daily checkpoint:
  1. CI matrix pass rate
  2. Quality gate status
  3. Remaining automation gaps

## Program-Level Reporting Cadence
1. Daily standup report in `docs/review/program_status.md` (new file updated daily).
2. End-of-week readiness snapshot updates:
- `quality_gate_report.md`
- `risk_register.md`
- `requirements_traceability_matrix.md`
