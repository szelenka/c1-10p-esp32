# Agent Team (generated)

> Source: `.agents/manifest.json`. Regenerate with `python3 .scripts/render_agent_docs.py`.

Role-specific instructions live in `.agents/personas/`. Each file adds role-specific context on top of CLAUDE.md.

| Agent | File | Writes To | Read-Only? | Load When |
|-------|------|-----------|------------|-----------|
| orchestrator | `.agents/personas/orchestrator.md` | nowhere (planning only) |  | Multi-subsystem tasks, unclear scope |
| architect | `.agents/personas/architect.md` | docs/design/ |  | Design decisions, capacity checks |
| implementer | `.agents/personas/implementer.md` | main/ (except hal/adapters), Makefile, .scripts/, configs, docs/registry.md |  | Most implementation work |
| tester | `.agents/personas/tester.md` | test/, Makefile test section |  | Writing or modifying tests |
| hardware | `.agents/personas/hardware.md` | main/*/hal/, main/*/adapters/ |  | HAL/driver/serial changes |
| telemetry-ui | `.agents/personas/telemetry-ui.md` | tools/telemetry_ui/, description/ |  | UI dashboard changes |
| docs | `.agents/personas/docs.md` | docs/ |  | Documentation updates |
| reviewer | `.agents/personas/reviewer.md` |  | yes (findings) | Pre-merge review |
| safety-auditor | `.agents/personas/safety-auditor.md` |  | yes (findings) | Motor/servo/safety changes |

Shared workflow policy lives in `.agents/policies/working-agreement.md`.
