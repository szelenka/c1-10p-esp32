# Agent Workflow Reference

> On-demand reference. Core rules are in `CLAUDE.md`. Load this file when you need task-mode details, multi-agent coordination, or pre-flight exceptions.

## Single-Agent Fast Path

Most sessions are single-agent, single-session. To avoid spending context on multi-agent ceremony:

- **Read**: `CLAUDE.md` + the one matching `.agents/personas/*.md` file for your area (see Quick Triage).
- **Skip**: `.agents/policies/working-agreement.md`, handoff YAML, `STATUS.md`, `make check-handoff` — unless the task is multi-phase or you're using subagents.
- **Skip**: structured output templates from reviewer/safety-auditor — fix issues inline and note what you fixed.

## Task Modes

Pick the lightest workflow that safely fits the task. Do not default every task to the full pipeline.

| Mode | Typical Scope | Required Verification |
|------|---------------|-----------------------|
| `planning-only` | triage, decomposition, no file edits | none |
| `docs-only` | `docs/`, `CLAUDE.md`, `.agents/` text updates | `make check-docs` |
| `ui-only` | `tools/telemetry_ui/`, `description/` | `make check-ui`; add `make test-ui-integration` for parser/frame changes |
| `single-subsystem code` | one implementation area, tightly coupled change | pre-flight `make test`, role gate, final integrator gate if finishing |
| `safety-critical` | motor, servo, safety, actuator path | pre-flight `make test`, `make check-safety`, role gate, final integrator gate |
| `cross-cutting parallel` | 2+ mostly independent areas | pre-flight `make test`, per-role gates, explicit reservations, final integrator gate |

## Pre-Flight Exceptions

- `planning-only`: skip pre-flight commands if no files will be edited
- `docs-only`: `git status` + `make check-docs`
- `ui-only`: `git status` + `make check-ui` (and `make test-ui-integration` if parser/frame behavior changed)
- `review-only`: reuse fresh evidence where allowed; run reviewer gate before sign-off

## Additional Build Targets

```bash
make format               # clang-format all source files (fast, run after every edit)
make check-format         # clang-format dry-run (fails if unformatted)
make lint-tidy            # clang-tidy baseline-gated (~3 min, run at end)
make lint-tidy-detail     # clang-tidy with full warning output (for diagnosing)
make lint-tidy-fix        # clang-tidy auto-fix
make lint-embedded        # banned heap types and RTTI check
make check-handoff        # validate .agents/handoff/current_handoff.yaml for multi-agent tasks
make check-ui             # validate telemetry UI (Python syntax + JS lint)
make test-ui-integration  # feed sample TEL: lines through parser, validate output
make check-ui-mapping     # validate joint_mapping.json against the active URDF
make agent-gate-fast      # single-agent readiness gate (all checks: test + lint + format + safety + docs)
make agent-gate-collab    # multi-agent readiness gate (includes handoff validation)
make render-agent-docs    # regenerate trigger rules, golden tasks, and agent team from .agents/manifest.json
make agent-gate           # alias for agent-gate-fast
```

### Recommended Iteration Loop

For fastest feedback during development, use this two-stage approach:

1. **During development** (fast, seconds): `make test && make format`
2. **Before handoff** (thorough, ~3 min): `make agent-gate-fast`

Do NOT run `make agent-gate-fast` on every edit — `lint-tidy` takes ~3 minutes. Use it once at the end.

## Human Quickstart

When joining a task already in progress:

1. Read [`.agents/handoff/STATUS.md`](.agents/handoff/STATUS.md) for a quick glance, then [`current_handoff.yaml`](.agents/handoff/current_handoff.yaml) for full state.
2. Run `make plan-triggers FILES="..."` for your intended edits before touching files.
3. Read the required `.agents/personas/*.md` files for the planned scope.
4. If the task is multi-agent, keep `current_handoff.yaml` and `STATUS.md` updated and run `make check-handoff` before handoff or sign-off.

Minimum required before handoff — see `.agents/policies/working-agreement.md` Human Minimums table.

## Context Exhaustion Guard

Use this heuristic to avoid losing work when the task is getting large:

- If you've made **15+ tool calls** or **edited 10+ files**, emit a CHECKPOINT (see `.agents/policies/working-agreement.md` Checkpoint Convention) regardless of perceived context pressure.
- Commit only when the user requested it or the workflow explicitly calls for it.
- If the task has more phases remaining, end with a resumption note the user can paste into a new conversation.

## Task Request Template

Use this when handing work to a human or agent (also in `.agents/policies/working-agreement.md`):

```text
Goal:
Allowed scopes:
Forbidden scopes:
Safety impact:
Desired verification:
Parallel allowed?:
New topic or node?:
Actuator path touched?:
Registry update expected?:
Limit increase requested?:
```

## Common Agent Rules (inherited by all agents)

All agents inherit these rules. Individual agent files only specify their **delta** — do NOT duplicate these rules in agent files.

- **Read scope**: any file (all agents can read everything)
- **Pre-flight owner**: one agent per task runs `make test` before changes begin. Other agents reuse that result unless the evidence is stale (see `.agents/policies/working-agreement.md`).
- **Role gate**: each writing agent runs only the checks required for its area before handoff. Avoid rerunning the full suite in every phase.
- **Final integrator gate**: one agent, usually the last writing agent, runs `make agent-gate-fast` (which includes test + lint-tidy + lint-embedded + check-format + check-safety + check-docs) before declaring the task done.
- **Registry ownership**: follow the registry policy in `.agents/policies/working-agreement.md`
- **Cross-cutting refactors** (renames, API changes affecting 3+ subsystems): use implementer agent but read `.agents/personas/orchestrator.md` first for sequencing
- **Read-only agents** (reviewer, safety-auditor): produce findings as text — the **implementer** or **tester** acts on them
- **Handoff protocol**: when completing a phase of a multi-phase task, emit a structured handoff including file reservations (see `.agents/policies/working-agreement.md` Handoff Protocol)
- **Structured artifact**: for multi-agent tasks, mirror the latest handoff/reservation state in `.agents/handoff/current_handoff.yaml` and update `.agents/handoff/STATUS.md`
- **Handoff validator**: for multi-agent tasks, run `make check-handoff` before handoff or final sign-off

## Finding Severity Levels

All agents use this shared taxonomy when reporting findings:

| Level | Meaning | Action |
|-------|---------|--------|
| BLOCKING | Safety violation or correctness bug | Must fix before merge. Stop pipeline. |
| ERROR | Hard rule violation, missing test, build failure | Must fix before merge. Pipeline continues. |
| WARNING | Style issue, missing doc, suboptimal pattern | Should fix. Can merge with user approval. |
| NOTE | Suggestion, observation, minor improvement | Optional. Informational only. |

Safety-auditor findings are BLOCKING or ERROR by default. Reviewer findings use all four levels.
