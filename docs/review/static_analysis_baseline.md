# Static Analysis Baseline

Date: 2026-02-28

## Tooling
- Primary tool: `cppcheck`
- Invocation target: `make analysis-cppcheck`

## Scope
- `main/chopper`
- `main/include/chopper`

## Gate Policy
1. CI runs static analysis on pull requests and mainline updates.
2. New critical findings should fail the gate.
3. Existing non-critical findings are tracked and reduced over time.

## Notes
- This baseline document establishes process and entrypoints.
- Detailed finding inventory should be added after first CI execution artifact is available.
- Local execution in this workspace currently reports missing `cppcheck` binary.
