# Agent Workflow — On-Demand Reference

> Task modes, verification rules, and skip conditions are in CLAUDE.md.
> Load this file only for severity definitions or manual verification gap details.

## Finding Severity

| Level | Meaning | Action |
|-------|---------|--------|
| BLOCKING | Safety violation or correctness bug | Stop pipeline. Fix before merge. |
| ERROR | Hard rule violation, missing test | Fix before merge. |
| WARNING | Style, missing doc | Should fix. Can merge with user approval. |
| NOTE | Suggestion | Optional. |

## Manual Verification Gaps

| Check | Why manual | Mitigation |
|-------|-----------|------------|
| Assertions weakened | Net-count heuristic misses semantic weakening | `make check-assertions` catches net reduction; review remaining cases by reading diff |
| Placement correctness | Anti-pattern check can't catch novel mistakes | `make check-placement` catches known anti-patterns; still state reasoning before editing |
| Transitive safety paths | Path rules can't detect all chains | Trace edit to all downstream consumers; see CLAUDE.md §Semantic Triggers |
| Context exhaustion | Token count not exposed | Use tool-call/file-edit counts as proxy (see CLAUDE.md Context Budget, mode-dependent limits) |
| known_failures growth | No gate prevents adding entries to avoid fixes | `known_failures.txt` is for pre-existing failures only; never add entries for bugs you introduced |
