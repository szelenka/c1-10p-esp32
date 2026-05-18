# Checkpoint Format

> Actual checkpoints go in `CHECKPOINT-<role>.md`. Each agent gets its own file.

## Template

```yaml
---
task_id: "<plaintext slug, e.g. add-periscope-random-motion>"
task_description: "<one-line description of the task>"
role: "<persona hat>"
status: "complete | partial | blocked"
owned_files: ["<files this agent owns>"]
read_files: ["<files loaded for this task>"]
produced_artifacts: ["<docs, generated files, diffs, or reports produced>"]
depends_on: ["<other CHECKPOINT slug, if any>"]
verification_run: ["<commands already run>"]
verification_pending: ["<commands still required>"]
verification_fresh_as_of: "<ISO 8601>"
blockers: ["<blocking issue, or none>"]
assumptions: ["<important assumption, or none>"]
merge_hazards: ["<shared files / generated artifacts / stale verification risks>"]
timestamp: "<ISO 8601>"
---
```

## Sections

```markdown
## Changed Files
## Not Changed (and why)
## Verification Passed
## Verification Still Required
## Open Questions
## Resume Note
<!-- Paste into new conversation to continue -->
```

## Matching

Match when: task_id slug semantically matches current task AND status is `partial` or `blocked`.
`complete` → delete the checkpoint.
Stale (different task) → ask user whether to delete or keep.

Verification freshness rule: any later merge into an owned or dependent file-set may invalidate prior checks. If freshness is unclear, re-run the listed verification before resuming.

## Discovery

On cold start: list ALL `CHECKPOINT-*.md` files, not just expected role. Previous session may have used a different persona.

## Dependency Check

Before resuming from a checkpoint with `depends_on` entries: verify all referenced checkpoints have `status: complete`. If any dependency is `partial` or `blocked`, do not resume — resolve the dependency first or ask the user.

## Multi-Agent Notes

- Each checkpoint must declare owned files explicitly. No two active agents may own the same writable file.
- Shared generated artifacts count as merge hazards even if source files are disjoint.
- The orchestrator owns final stale-verification detection and final gate execution.
