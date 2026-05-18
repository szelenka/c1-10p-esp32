# Placement Reasoning

> Implement logic at the **earliest correct point** in the pipeline.

## Pipeline

```
Input capture → Intent mapping → Action node → Bridge node → Driver → Hardware
                                                                        |
              UI (app.js) ← WebSocket ← app.py ← TelemetryIOTapNode ←-+
```

TelemetryIOTapNode taps multiple topics in parallel. Full topology: `docs/registry.md`.

## Stage Ownership

| Stage | Responsibility | Key files |
|-------|---------------|-----------|
| Input capture | Raw controller state, connections | `BluepadInputNode.h` |
| Intent mapping | Button/axis → semantic intent | `DriveIntentMapping.h`, `DomeIntentMapping.h` |
| Input processing | Sequences, combos, debounce | `BluepadInputNode.h` or dedicated node |
| Action node | React to intents, produce commands | `DriveNode.h`, `DomeNode.h`, `PeriscopeNode.h` |
| Bridge node | Safety-gated forwarding | `MotorBridgeNode.h`, `ServoBridgeNode.h` |
| Driver/adapter | Hardware protocol | `hal/`, `adapters/` |
| Telemetry tap | Passive observation | `TelemetryIOTapNode.h` |
| Telemetry bridge | Serial + WebSocket | `app.py` |
| Telemetry UI | Visualization | `web/app.js`, `web/render3d.js` |

## Anti-Patterns

| Task | Wrong | Right | Why |
|------|-------|-------|-----|
| Button X → action Y | Action node checks raw button | Intent mapping table | Button-to-intent is input-layer |
| Detect combo/hold | Action node | BluepadInputNode | Needs raw input over time |
| Serial port setup | `app.py` | Makefile `run-ui-bridge` | Build infrastructure |
| New telemetry field | Only `app.js` | Firmware → tap → parser → UI | Each layer forwards |
| Change button action | Action node | Intent mapping | Remapping is intent-layer |
| Motor no response | Retry in action node | Trace bridge → driver → HW | Break is in delivery chain |
| Smooth telemetry | `app.js` / `render3d.js` | Firmware node | Filter at source |
| Transform for display | Firmware node | `app.py` / `app.js` | Display is UI-layer |

## Decision Process

1. Read data flow in `docs/registry.md`
2. Identify pipeline stage from table above
3. State: "This belongs in [file] because it is [stage] logic"
4. Check: does this file need to know about another stage's concerns? If yes, wrong place.

## Smell Tests

Wrong if: action node has button bitmasks · downstream node duplicates upstream state · `app.py` gains logic beyond parsing/serving · bridge node makes decisions beyond gating · you're adding a workaround for something another layer should provide.
