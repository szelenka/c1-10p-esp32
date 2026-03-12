# Chopper — Agent Entry Point

Read [`CLAUDE.md`](CLAUDE.md). It is self-contained for 90% of tasks: rules, routing, role capsules, verification.

Order: Quick Start → Task Patterns → Task Routing → Role Capsules → edit → verify.

For safety-critical or cross-cutting tasks: also load [`.agents/extended.md`](.agents/extended.md) (referenced sections only).

For trivial edits: [.agents/express.md](.agents/express.md) (single file, <20 lines, no safety).

<!-- BEGIN GENERATED: minimum-verification -->
Minimum verification: `make test` + `make format`; if actuator path reachable, also run `make check-safety`.
<!-- END GENERATED: minimum-verification -->
Capability degradation table: `.agents/policies/cross-tool.md`.

## Minimum Viable Instructions (fallback)

If your tool cannot load CLAUDE.md automatically, use this block:

```
Project: ESP32 C++20 embedded astromech. Zero-allocation pub/sub hot path.
Pipeline: Input capture → Intent mapping → Action node → Bridge node → Driver → Hardware.
Vocabulary: Intent mapping = button→action tables. Action node = command logic. Bridge node = safety-gated forwarding. Area = route group (safety, hardware, implementation, tests, telemetry-ui, docs).

HARD RULES:
1. Button mapping in intent table (DriveIntentMapping.h), NEVER in action node.
2. NEVER weaken test assertions. Fix code, not tests.
3. ALL motor/servo commands gated by DegradationManager. No bypass — even transitive.

BANNED on hot path: new, delete, malloc, std::string, std::vector, std::function, std::unordered_map, dynamic_cast, typeid.
Style: snake_case functions, PascalCase types, ALL_CAPS constants, #pragma once.

VERIFY: make test && make format (after every edit). make check-safety (if actuator path reachable).
DONE: make test passes, make format applied, placement reasoning stated, safety checked if relevant, make agent-gate-fast for non-trivial tasks.

Key paths: main/include/chopper/ (headers), main/chopper/ (source), test/test_*.cpp (tests).
Limits: main/include/chopper/chopper_limits.h. Messages: main/include/chopper/messages/CommonMessages.h.
```

For persona capsules (compressed role summaries), see `.agents/policies/cross-tool.md` §Persona Capsules.
