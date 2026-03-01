# Risk Register

Date: 2026-02-28  
Scope: quality/readiness risk tracking for current workspace

## Scale
- Likelihood: Low / Medium / High
- Impact: Low / Medium / High / Critical

## Active Risks

| ID | Risk | Likelihood | Impact | Evidence | Mitigation | Owner |
|---|---|---|---|---|---|---|
| R-001 | Remaining requirement gap for explicit WiFi integration/validation | Medium | Medium | Telemetry + HTTP/WebSocket implemented; no bench-level WiFi validation evidence yet | Add target WiFi bring-up and endpoint validation in HIL cycle | Product + Architect |
| R-002 | Runtime parameter persistence requires target-device validation | Medium | Medium | Persistence implemented in `ParameterServer.cpp`; host tests added | Validate on-device NVS behavior in HIL cycle | Core FW Engineer |
| R-003 | Safety confidence based mostly on host tests, limited hardware proof | Medium | Critical | Test suite is host-only (`make test`) | Add HIL tests for motor/servo/audio/safety fault paths | Verification Engineer |
| R-004 | Documentation drift may create false positives in architecture reviews | Medium | Medium | `docs/design/core-framework-audit.md` partially stale vs `Executor.cpp` | Refresh audit docs per release and cross-link commit hash/date | Architect |
| R-005 | Cross-platform support not continuously evidenced | Medium | Medium | No CI matrix evidence in repo for Win/Linux/macOS | Add CI jobs or periodic documented matrix runs | DevOps/Tooling |
| R-006 | Uncommitted workspace state may hide integration regressions before release | Medium | Medium | `git status` shows many modified/untracked files | Freeze release candidates from tagged commit + repeat full verification | Release Manager |

## Watchlist (Lower Priority)

| ID | Item | Reason |
|---|---|---|
| W-001 | Formal static analysis gate | Improves maintainability and catches defects earlier |
| W-002 | Explicit telemetry acceptance tests | Needed if telemetry remains in scope |
| W-003 | Requirement-to-test coverage automation | Reduces manual drift in traceability |

## Next Review Trigger
Re-run this register when one of these occurs:
1. WiFi endpoint validation on target hardware completes
2. Hardware-bench HIL cycle completes
3. Release candidate checklist sign-off begins
