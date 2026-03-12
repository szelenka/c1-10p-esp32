# Agent Lessons Learned

> Record mistakes that got past gates. Add a lesson when an agent error is discovered.
> After a difficult task, consider adding a near-miss or lesson entry.

## Templates

### Lesson (error that landed)
```markdown
### YYYY-MM-DD: Title
**Error**: What happened.
**Gate**: Which check should have caught it.
**Root cause**: Why it didn't.
**Fix**: What changed to prevent recurrence.
```

### Near-miss (error caught before landing)
```markdown
### YYYY-MM-DD: Near-miss — Title
**Almost**: What nearly happened.
**Caught by**: Which gate/check prevented it.
**Why close**: Why the agent nearly missed it.
```

When adding: also update the relevant gate (common-mistakes, trigger rules, persona checklist, or Makefile target).

## Lesson Graduation

Lessons that recur 3+ times should be promoted:

| Recurrence | Promote to | Action |
|-----------|-----------|--------|
| 2 occurrences | Common Mistakes table in CLAUDE.md | Add entry with `make` target |
| 3+ occurrences | Hard Rules or gate improvement | Add/improve a `make check-*` target |
| Pattern across personas | Persona checklist or trigger rule | Update affected persona + trigger-rules.md |

Review this file every 10 tasks for graduation candidates. Mark graduated lessons with `[GRADUATED → <target>]`.

---

## Lessons

### 2026-03-11: Safety gate passed despite shallow enforcement
**Error**: `check_safety.sh` matched `DegradationManager` in comments. Bash ordering check duplicated Python with different bugs.
**Gate**: `make check-safety`, `make check-safety-ordering`.
**Root cause**: Regex matched comments. Duplicate bash/Python implementations diverged.
**Fix**: Consolidated into `check_safety_ordering.py` with comment filtering. Removed bash duplicate. Added to `agent-gate-fast`.

### 2026-03-11: Stale known_failures not gated
**Error**: Fixed test still in `known_failures.txt`. Makefile warned but didn't fail.
**Gate**: `make test`.
**Root cause**: Stale entries were informational only.
**Fix**: `make test` now exits non-zero when known_failures entries pass.

### 2026-03-11: Telemetry UI not updated when firmware format changed
**Error**: Multiple instances where firmware telemetry fields were added or modified but the Python parser (`app.py`) and JavaScript handler (`app.js`) were not updated. Silent failure due to lenient parser.
**Gate**: No gate existed to catch this.
**Root cause**: No semantic trigger linked firmware telemetry files to UI files. Routing only triggered telemetry-ui persona when UI files themselves were touched. The parser's intentional leniency masked mismatches.
**Fix**: Added `make check-telemetry-sync` (`.scripts/check_telemetry_sync.py`) with a maintained schema contract. Added telemetry semantic triggers to CLAUDE.md. Wired into `check-ui` and `agent-gate-fast`. Added to implementer Watch For and telemetry-ui Done When.

### 2026-03-11: behaviors.md had 4 stale button mappings
**Error**: Wrong buttons listed for dome roam, eye color, carpet mode. Missing face tracking hold note.
**Gate**: `make check-docs`.
**Root cause**: `check_docs.py` doesn't validate behavioral descriptions.
**Fix**: Fixed entries. Future: consider button-mapping validation in `check_docs.py`.
