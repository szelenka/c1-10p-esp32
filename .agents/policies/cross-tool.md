# Cross-Tool Entry Points

<!-- BEGIN GENERATED: entry-points -->
| Tool | Entry | Notes |
|------|-------|-------|
| Claude Code | `CLAUDE.md` | Auto-loads repository instructions. |
| Codex | `AGENTS.md -> CLAUDE.md` | Bootstrap from AGENTS.md, then use the canonical shell. |
| Cursor | `.cursorrules -> CLAUDE.md` | Keep editor rules as a thin redirect. |
| Windsurf | `.windsurfrules -> CLAUDE.md` | Keep editor rules as a thin redirect. |
| No file loading | `Paste CLAUDE.md Minimum Viable Instructions` | Use compact fallback blocks plus persona capsules. |
<!-- END GENERATED: entry-points -->

<!-- BEGIN GENERATED: minimum-verification -->
Minimum verification: `make test` + `make format`; if actuator path reachable, also run `make check-safety`.
<!-- END GENERATED: minimum-verification -->
Multi-agent is opt-in only — see `orchestrator.md` Multi-Agent section.

## Capability Degradation

<!-- BEGIN GENERATED: capability-degradation -->
| If tool can't... | Do this instead |
|---|---|
| Run make targets | Use the manual checklist from Constraints plus diff review and call out missing verification. |
| Launch subagents | Stay single-agent and switch roles sequentially. |
| Count tokens | Use tool-call and file-read thresholds as the context proxy. |
| Access shell | Do read-only review and emit file:line findings with severity. |
| Load files automatically | Paste Minimum Viable Instructions and the matched persona capsule. |
| Load arbitrary files on demand | Use the matched golden task plus persona capsules and touch-set only reads. |
<!-- END GENERATED: capability-degradation -->

## Context-Constrained Mode

For models with <32k token context or when operating under tight budgets:

<!-- BEGIN GENERATED: context-constrained-mode -->
1. Load .agents/express.md plus the matched golden task entry.
2. Load the matched persona capsule instead of the full persona file.
3. Run the shared minimum verification.
4. If safety-relevant, also run the conditional safety gate.
5. Skip agent-gate-fast unless the golden task or user explicitly requires it.
<!-- END GENERATED: context-constrained-mode -->

## Persona Capsules

Use these compressed role summaries when the tool cannot afford the full persona files.

<!-- BEGIN GENERATED: persona-capsules -->
### implementer
- Own production-code changes and state placement reasoning before editing.
- Escalate to safety-auditor if the change can reach an actuator.
- Run lint-embedded for code changes and update docs/registry when interfaces move.

### tester
- Use doctest for new tests and do not weaken assertions.
- Root-cause failures as test bug vs code bug before changing behavior.
- Verify Makefile DEPS coverage with make check-test-inventory when adding tests.

### safety-auditor
- Treat actuator reachability as BLOCKING until safety gating is proven.
- Trace commands through DegradationManager and bridge nodes, not comments alone.
- Run make check-safety and make check-safety-ordering for safety-relevant diffs.

### docs
- Source code is authoritative for behavioral and API claims.
- Instruction edits must preserve valid cross-references and remove duplicated rules.
- Run make check-docs after changing docs, registry, or .agents content.

### orchestrator
- Single-agent sequential role switching is the default.
- Use multi-agent only for disjoint file sets, declared ownership, and explicit merge order.
- The orchestrator owns stale-verification detection and final gate execution.
<!-- END GENERATED: persona-capsules -->

## Context-Size Routing

| Effective context | Load strategy |
|-------------------|---------------|
| >100k tokens | Full CLAUDE.md + matched persona file |
| 32k-100k tokens | CLAUDE.md + persona capsule (below) instead of full persona file |
| <32k tokens | Minimum Viable Instructions (in CLAUDE.md and AGENTS.md) + matched persona capsule |

## Paste Block for No-File-Loading Tools

Copy the Minimum Viable Instructions section from the bottom of CLAUDE.md (also inlined in AGENTS.md) into your system prompt. It covers hard rules, banned patterns, verification, and key paths in ~15 lines.
