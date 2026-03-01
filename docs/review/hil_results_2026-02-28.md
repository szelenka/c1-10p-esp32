# HIL Results

Date: 2026-02-28
Status: Hardware-assisted validation completed on ESP32 dev board (simulated peripherals)

## Executed Scope (Fallback)
1. Verified safety and integration paths through host test suites:
- `test_safety`
- `test_integration`
- `test_telemetry`
2. Verified emergency-stop propagation paths and telemetry snapshot generation.
3. Verified quality gates:
- `make test` pass
- `make traceability-check` pass
- `make analysis-cppcheck` pass

## Result Summary
- Software behavior coverage is strong in host environment.
- This does not replace physical actuator timing measurements and fault-response latency on target hardware.

## Executed Scope (Hardware-Assisted)
1. Flashed `esp32dev-validation` firmware to `/dev/cu.usbserial-59691018321`.
2. Ran automated validation script:
- `./scripts/run_on_device_validation.sh --env esp32dev-validation --port /dev/cu.usbserial-59691018321 --seconds 30`
3. Observed required serial markers:
- `NVS_SELFTEST:PASS loaded_drive_max_speed=0.570`
- `TEL:{...}`
4. HTTP checks were intentionally skipped (no host provided), consistent with current validation mode.

## Hardware Result Summary
- Build: PASS
- Upload: PASS
- Serial marker validation: PASS
- Overall script result: PASS

## Noted Observation
- During runtime, `telemetry_node` exceeded configured executor limit (`15938 us` vs `1000 us`) and triggered an emergency stop once.
- This did not fail marker-based validation but should be tracked as a performance/scheduling follow-up before release.

## Remaining Hardware Actions
1. Re-run with real peripherals attached and execute safety trigger scenarios from `hil_test_plan.md`.
2. Measure and record trigger-to-safe-state latency under representative load.
3. Decide whether telemetry node budget should be relaxed or node workload reduced to avoid timeout-induced E-stop.
