# Working Agreement

> See `CLAUDE.md` for project-level rules, build commands, hard constraints, and trigger rules.

This file defines the shared collaboration model for humans and subagents.

## Task Intake

For fastest execution, include these in the task request when known:

- Desired outcome
- Affected subsystem(s) or file area
- Whether actuators / safety-critical behavior are involved
- Expected verification (`make test`, `make check-safety`, UI checks, docs-only, etc.)
- Whether parallel subagents are desired or acceptable
- Any scope limits ("no new messages", "no HAL changes", "tests only", etc.)

Copy-paste template:

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

## Execution Modes

Choose the lightest mode that safely fits the task:

| Situation | Mode | Expected Behavior |
|-----------|------|-------------------|
| One subsystem, tightly-coupled edit, fast turnaround | Single-thread role switch | One agent works locally and changes hats as needed |
| Two or more mostly independent scopes | Parallel subagents | Spawn subagents with disjoint ownership and a clear integrator |
| Safety-critical or unclear scope | Human checkpoint | Clarify scope before parallelizing |

Default rule: if the next step depends on code from another role, stay local; if work can proceed independently, parallelize it.

## Express Lane (experienced contributors)

For changes where ALL of these are true:

- Single file edit, no new APIs or message types
- No actuator/safety path touched
- Change is < 20 lines of behavioral code
- No new node, topic, or limit increase

Minimum process: read this file's matching role file, run `make test`, and emit a short checkpoint/result summary. Skip: handoff YAML and structured evidence format. Trigger reads beyond the relevant role file may be skipped when no additional scope is involved.

The express lane does NOT exempt you from: Hard Rules (CLAUDE.md), safety gating, or running `make test`.

## Source Of Truth

When code, docs, agent instructions, or examples disagree:

- Source code and executable checks win over prose docs
- `chopper_limits.h` wins for limit values
- Safety implementation plus `make check-safety` wins for safety invariants
- The agent must flag the stale doc or instruction immediately and update it in the same task when in scope

## Evidence Freshness

Verification evidence is considered **stale** when any of these happen after it was gathered:

- A new commit lands on the branch
- A merge, rebase, or conflict resolution changes the affected area
- Another agent edits the same subsystem or reserved file set
- A safety-path change occurs after a prior safety audit

When evidence is stale, rerun the relevant role gate before relying on it.

## Verification Model

Use staged verification:

- **Pre-flight owner**: one agent per task runs `make test` before changes begin
- **Role gate**: each writing agent runs only the checks required for its area before handoff
- **Final integrator gate**: one agent, usually the last writing agent, runs `make test` + `make format` + `make lint-tidy` + `make lint-embedded` + `make check-docs` before declaring the task done (or just `make agent-gate-fast` which runs all of these)

### Verification Evidence Format

When handing off verification, use this format:

```text
EVIDENCE:
- command: make test
  result: PASS
  basis: working tree after current phase / commit abc123 / no further edits in test scope
  timestamp: 2026-03-10 14:30 ET
```

## Human Minimums

Before handing work to another human or agent, provide at least:

| Task Mode | Minimum Required |
|-----------|------------------|
| `planning-only` | owner, next action, open questions |
| `docs-only` | `make check-docs` result + human summary |
| `ui-only` | `make check-ui` result, plus `make test-ui-integration` when schema/parser changed |
| `single-thread role switch` | latest writer, changed files, pending verification |
| `parallel subagents` | updated `.agents/handoff/current_handoff.yaml` + `make check-handoff` PASS |
| `human checkpoint` | exact blocker and decision needed |

## Skip Matrix

| Command | Required When | May Be Skipped When |
|---------|----------------|---------------------|
| `make test` | pre-flight owner; final integrator; tester final handoff | another agent already ran it and evidence is fresh |
| `make test-build` | implementer / hardware role gate | `make test` already passed after the same edit set |
| `make format` | any code change (fast, always run) | doc-only or UI-only work |
| `make lint-tidy` | final integrator gate; reviewer gate (baseline-gated, ~3 min) | doc-only or UI-only work with fresh prior evidence |
| `make lint-embedded` | reviewer gate; final integrator gate | doc-only or UI-only work with fresh prior evidence |
| `make check-format` | final integrator gate | `make format` already ran after the same edit set |
| `make check-docs` | docs / architect role gate; final integrator gate | no docs or registry-relevant behavior changed and fresh prior evidence exists |
| `make check-safety` | any motor/servo/safety-path change | never for relevant diffs |
| `make check-ui` | telemetry UI changes | non-UI tasks |
| `make test-ui-integration` | telemetry parser / frame / visualization changes | non-UI tasks or cosmetic-only UI edits |

Only the current phase owner or final integrator may declare a check skipped.

## Handoff Protocol

When completing a phase of a multi-phase task, emit a structured handoff so the next agent or human has full context:

```
HANDOFF: [source_agent] -> [target_agent]
Goal: [what this phase was trying to accomplish]
Primary owner for next phase: [agent]
Files reserved for edit: [list]
Scopes reserved for edit: [list or none]
Advisory-only roles: [list or none]
Files changed: [list of files added/modified]
Files to read: [files the next agent should read first]
Decisions made: [key choices]
Acceptance criteria: [what must be true for the next phase to succeed]
Open questions: [anything the next agent needs to decide or confirm with user]
Blocked by: [none, or exact blocker]
Registry updates needed: [new/changed topics, nodes, or message types]
Commands run: [which commands already passed]
Commands still required: [what the next agent or integrator still must run]
Risk notes: [known risk, ambiguity, or follow-up area]
Evidence: [use the Verification Evidence Format above]
Human summary:
- What changed: [one-line summary]
- Why: [one-line reason]
- Read these files first: [key files]
```

After emitting a handoff, also update:

- `.agents/handoff/current_handoff.yaml` with ownership, reservations, verification state, and **increment the `sequence` field** (monotonic counter for conflict detection)
- `.agents/handoff/STATUS.md` with the plain-English quick-glance summary

Run `make check-handoff` before handing off.

Handoffs are mandatory for multi-phase tasks. For single-agent tasks (e.g., tester-only), skip the handoff.

## Checkpoint Convention (for long tasks)

For tasks spanning more than 3 phases, or when context is running low, emit a checkpoint after each completed phase:

```
CHECKPOINT: Phase 2/4 complete
Completed: [what's done, files changed]
Remaining: [phases left, what each needs to do]
Resumption: read [key files] to continue in a new conversation
Verification so far: [which make targets passed]
```

If context is approaching limits, emit a final checkpoint the user can paste into a new conversation to resume. Commit only if the user requested it or the workflow explicitly calls for it.

## File Reservation Rule

To avoid merge conflicts between subagents:

- Reservations may be at file level or scope level (`test/`, `tools/telemetry_ui/`, `Makefile` test section, etc.)
- Each handoff must declare the **primary owner for the next phase**
- Each handoff must list **files reserved for edit** and may also list **scopes reserved for edit**
- Agents outside that reservation are advisory-only unless the owner reassigns the file
- If two roles need the same file, one becomes the writer and the other stays review-only for that file
- Reservations may only be changed by a new handoff or an explicit final-integrator decision

## Shared Choke Points

Use explicit ownership for commonly contested files:

| Path / Scope | Default Writer | Advisory Role(s) | Notes |
|--------------|----------------|------------------|-------|
| `Makefile` test section | tester | implementer | Keep edits limited to test targets and `TEST_BINS` |
| `Makefile` non-test sections | implementer | tester, telemetry-ui | Final integrator resolves overlap |
| `docs/registry.md` during code-changing tasks | implementer | docs | Land in integration phase when possible |
| `docs/registry.md` during doc-only sync | docs | implementer | Use source code as truth |
| `platformio.ini`, `sdkconfig.defaults` | implementer | architect | Treat as system-wide config |
| `tools/telemetry_ui/joint_mapping.json` | telemetry-ui | hardware | Hardware flags mapping changes; UI owns edits |

## Structured Handoff Artifact

For multi-agent tasks, the latest reservation and handoff state must also be mirrored in:

- `.agents/handoff/current_handoff.yaml` — durable machine-readable state
- `.agents/handoff/STATUS.md` — plain-English quick-glance summary

Use the YAML as the source of truth for: current primary owner, reserved files/scopes, outstanding verification, and open questions. Use STATUS.md as the 5-second human onboarding path.

The YAML must include a short human summary: `what_changed`, `why`, `read_first`.

## Escalation Protocol

When agents or humans disagree on approach, scope, or findings:

1. The disagreeing party adds an `open_questions` entry to `.agents/handoff/current_handoff.yaml` with `escalate_to: architect|user|safety-auditor`
2. Work on the disputed area **stops** until resolution — other non-conflicting work may continue
3. Resolution priority: safety-auditor wins safety disputes, architect wins design disputes, user wins scope disputes
4. The resolution is recorded in the YAML (`open_questions` entry updated with decision and rationale) before work resumes
5. If no response within a reasonable time, the agent should ask the user directly rather than guessing

## Registry Policy

`docs/registry.md` rules:

- Implementer is the primary owner during code-changing tasks
- Docs may update it for doc-only sync tasks
- If code and registry both change in the same task, prefer the implementer to land the registry update in the integration phase

## Sub-Agent Delegation

When spawning a sub-agent (regardless of tool — Claude Code Agent tool, Codex task, or any other multi-agent system):

1. **Always include in the prompt**:
   - "Read `CLAUDE.md` and `.agents/personas/<role>.md` before making any edits"
   - The specific files this sub-agent is allowed to edit (its reservation set)
   - The verification command(s) to run before returning results
   - "Do NOT edit files outside: [list]"

2. **Never assume context inheritance** — subagents start with a blank slate. They do not automatically see CLAUDE.md, prior conversation, or project conventions.

3. **One writer per file** — if two subagents need the same file, one must be advisory-only for that file. Declare this in the prompt.

4. **Merge point** — designate which agent (or the parent) is the final integrator responsible for running `make agent-gate-fast` after all sub-agent results are collected.

5. **Keep prompts self-contained** — include the goal, allowed scope, forbidden scope, and acceptance criteria directly in the sub-agent prompt. Do not rely on the sub-agent reading handoff YAML unless it is a multi-session task.

## Exempt Change Rule

Comment-only, formatting-only, whitespace-only, and typo-only edits are exempt from trigger reads.

If an exempt change becomes behavioral later in the same task, required agent reads must happen before the first behavioral edit.
