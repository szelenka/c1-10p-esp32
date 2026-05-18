# Hardware-in-the-Loop (HIL) Test Plan

Date: 2026-02-28

## Objective
Validate that safety-critical behavior proven in host tests also holds on target hardware under fault conditions.

## Test Bench
- ESP32 target board compatible with this branch
- Motor driver path (Sabertooth/SyRen equivalent)
- Servo controller path (Maestro equivalent)
- Audio path (MP3 trigger equivalent)
- Bluetooth controller input path
- Instrumentation: serial logs and timing capture

## Scenarios
1. Controller timeout triggers safe actuator behavior.
2. Executor loop overrun path triggers emergency handling.
3. Manual emergency stop propagates through `SafetyManager` and driver shutdown.
4. Driver error path transitions system to degraded or emergency state as specified.

## Data to Capture
1. Trigger timestamp
2. Detection timestamp
3. Actuator command-to-zero timestamp
4. Final state confirmation

## Pass Criteria
1. All safety triggers result in bounded response and safe outputs.
2. No uncontrolled actuator behavior under tested faults.
3. Logs are sufficient to trace event path end-to-end.

## Artifacts
- `docs/review/hil_results_YYYY-MM-DD.md`
