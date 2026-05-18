# Build Commands

```bash
# Every task
make test                 # compile + run all host tests
make format               # clang-format apply
make agent-gate-fast      # full pre-merge gate (~3 min)
make agent-gate-json      # structured gate runner (GATE: lines + GATE_SUMMARY JSON)

# Selective
make preflight            # test-inventory + test-build + capacity + git status
make lint-tidy            # clang-tidy baseline-gated
make lint-tidy-changed    # clang-tidy on changed files only (fast)
make lint-embedded        # banned patterns (heap, RTTI)
make check-assertions     # detect weakened test assertions (net reduction)
make check-high-impact    # flag changes to high-impact files (user approval)
make check-placement      # pipeline placement anti-pattern detection
make check-safety         # safety gating invariants
make check-safety-ordering # safety gate ordering
make check-safety-paths   # */cmd publishers without bridge routing
make check-capacity       # resource usage vs limits
make check-docs           # stale docs detection
make check-test-inventory # test files wired into Makefile
make check-ui-mapping     # joint_mapping vs URDF
make check-triggers       # which personas to load for current diff
make ci-local             # agent-gate-fast + cppcheck
```
