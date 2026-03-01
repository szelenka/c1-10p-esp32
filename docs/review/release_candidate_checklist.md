# Release Candidate Checklist

Date: 2026-02-28

## Preconditions
1. Working tree cleanliness verified for release candidate cut.
2. No unresolved `High` risks without approved exception.
3. Scope decision record is current.

## Technical Gates
1. `make test` passes fully.
2. Static analysis gate run completed and reviewed.
3. Traceability check completed with no missing evidence for in-scope requirements.
4. Architecture audit docs are synchronized to current implementation.

## Safety/Validation Gates
1. HIL test plan exists and is current.
2. HIL execution evidence attached for safety-critical scenarios (or approved temporary waiver).
3. Emergency-stop behavior validated in integration path.

## Documentation Gates
1. `quality_gate_report.md` updated for current candidate.
2. `risk_register.md` updated and reviewed.
3. `requirements_traceability_matrix.md` updated.
4. Scope deferrals explicitly listed.

## Sign-off
- Core runtime lead:
- Safety lead:
- Verification lead:
- Product/requirements lead:
- Release manager:
