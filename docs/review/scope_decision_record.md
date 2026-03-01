# Scope Decision Record

Date: 2026-02-28  
Decision ID: SDR-2026-02-28-01

## Decision
For the current branch execution window:
1. **Implement now**: telemetry over serial and HTTP/WebSocket, plus minimal HTTP parameter API.
2. **Defer**: hardware-bench HIL execution until bench access is available (host-simulated fallback executed now).

## Affected Backlog Items
- B-003: Completed (scope decision approved in this document)
- B-004: Completed (minimal HTTP parameter API implemented)
- B-005: Completed (serial + HTTP/WebSocket telemetry implemented)

## Rationale
1. Telemetry and parameter APIs are required for near-term requirement fit.
2. Hardware bench unavailability should not block software delivery of runtime endpoints.
3. HIL remains a separate acceptance track with explicit residual risk.

## Required Follow-up
1. Keep `WiFi` requirement marked as pending until explicit WiFi runtime integration is validated.
2. Re-run quality gate after hardware-bench HIL execution to close remaining critical safety evidence risk.

## Owner Approval
- Product/Requirements owner: pending signature
- Architecture owner: pending signature
