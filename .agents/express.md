# Chopper — Express Path

> Use when ALL true: edit inside existing function, single file, <20 lines,
> no safety/API/node/topic/limit change. Otherwise use `CLAUDE.md`.

1. Follow CLAUDE.md [Hard Rules](CLAUDE.md#hard-rules) and [Constraints](CLAUDE.md#constraints)
2. Write the change
3. Run the minimum verification:
<!-- BEGIN GENERATED: minimum-verification -->
Minimum verification: `make test` + `make format`; if actuator path reachable, also run `make check-safety`.
<!-- END GENERATED: minimum-verification -->
4. Done

If you discover safety relevance, multiple subsystems, or a new test failure: switch to full `CLAUDE.md`.
