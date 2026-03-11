# Chopper — Agent Entry Point

These instructions apply to any coding agent working in this repository.

Start here, then load only the minimum additional context needed for the task.

## Read Order

1. Read [`CLAUDE.md`](/opt/_src/github/szelenka/chopper/CLAUDE.md).
2. Read the one matching file in [`.agents/personas/`](/opt/_src/github/szelenka/chopper/.agents/personas) for your area before making behavioral edits.
3. Read [`.agents/policies/working-agreement.md`](/opt/_src/github/szelenka/chopper/.agents/policies/working-agreement.md) only for multi-phase, parallel, or handoff-driven work.

## Fast Path

For most single-agent tasks:

- Read `CLAUDE.md` plus the relevant role file in `.agents/personas/`.
- Skip handoff artifacts, `.agents/handoff/current_handoff.yaml`, `.agents/handoff/STATUS.md`, and `make check-handoff` unless the task is multi-phase or uses parallel subagents.
- Skip structured reviewer/safety-auditor output templates in single-agent work; fix issues inline and note the role switch.

## Core Rules

- Follow the quick triage, task modes, hard rules, and verification model in `CLAUDE.md`.
- Safety-critical changes must run `make check-safety`.
- Behavioral edits must follow the trigger rules in `.agents/generated/trigger-rules.md`.
- Source code and executable checks win when docs and code disagree; update stale docs in the same task when in scope.

## Role Routing

- `.agents/personas/implementer.md`: production firmware and most code changes
- `.agents/personas/tester.md`: tests, mocks, Makefile test targets
- `.agents/personas/hardware.md`: HAL, adapters, serial protocol, driver changes
- `.agents/personas/telemetry-ui.md`: telemetry UI and `joint_mapping.json`
- `.agents/personas/docs.md`: documentation updates
- `.agents/personas/architect.md`: design decisions, capacity, cross-cutting constraints
- `.agents/personas/reviewer.md`: read-only review findings
- `.agents/personas/safety-auditor.md`: read-only actuator and safety audit findings
- `.agents/personas/orchestrator.md`: planning, sequencing, multi-area coordination

## Keep Context Small

- Do not load all role files by default.
- Do not load reference docs unless the current task needs them.
- Prefer the single-agent fast path unless the task is clearly multi-area, safety-critical, or blocked on coordination.
