# Requirements Traceability Matrix

Date: 2026-02-28  
Scope: current workspace in `/opt/_src/github/szelenka/chopper`

## Legend
- `Met`: implemented with code + test evidence
- `Partial`: implemented in part, missing required behavior or persistence
- `Planned`: present mainly in design docs, not implemented in runtime code
- `Not Evidenced`: requirement exists but no direct implementation/test evidence found

## Matrix

| Requirement Area (docs/INSTRUCTIONS.md) | Status | Evidence (Code) | Evidence (Tests) | Notes |
|---|---|---|---|---|
| Node-based modular architecture (ROS2-like on ESP32) | Met | `main/include/chopper/core/*.h`, `main/chopper/core/*.cpp`, `main/include/chopper/nodes/*.h` | `test/test_core.cpp`, `test/test_integration.cpp` | Pub/sub, node lifecycle, executor loop are implemented. |
| Deterministic periodic execution + watchdog behavior | Met | `main/chopper/core/Executor.cpp`, `main/include/chopper/core/Executor.h` | `test/test_integration.cpp`, `test/test_safety.cpp` | Uses `vTaskDelayUntil`, loop timeout and emergency-stop path. |
| Safety timeouts and safe actuator stop | Met | `main/include/chopper/safety/SafetyManager.h`, `EmergencyStopChain.h`, `MotorSafetyMonitor.h`, `main/chopper/safety/*.cpp` | `test/test_safety.cpp`, `test/test_integration.cpp` | Multiple safety components implemented and tested. |
| DC motor control abstraction (Sabertooth/SyRen-friendly) | Met | `main/include/chopper/hal/IMotorDriver.h`, `SabertoothMotorDriver.h`, `main/include/chopper/nodes/MotorBridgeNode.h` | `test/test_hal.cpp`, `test/test_sabertooth.cpp`, `test/test_integration.cpp` | Interface + concrete protocol behavior validated on host tests. |
| Servo controller abstraction (Maestro) | Met | `main/include/chopper/hal/IServoController.h`, `MaestroServoDriver.h`, `ServoBridgeNode.h` | `test/test_maestro.cpp`, `test/test_hal.cpp`, `test/test_integration.cpp` | Channel control and command encoding covered. |
| Audio trigger/control abstraction | Met | `main/include/chopper/hal/IAudioDriver.h`, `MP3AudioDriver.h`, `AudioBridgeNode.h` | `test/test_mp3trigger.cpp`, `test/test_integration.cpp` | Track, random pool, volume, and update behavior covered. |
| Controller role assignment and MAC-based behavior | Met | `main/include/chopper/bluetooth/*.h`, `main/chopper/hal/ChopperBluetooth.cpp` | `test/test_bluetooth.cpp` | Role manager, slot policies, disconnect behavior tested. |
| Advanced button/function mapping nodes | Met | `main/include/chopper/nodes/DriveNode.h`, `PeriscopeNode.h`, `DomeArmsNode.h`, `BodyUtilityNode.h`, `SoundNode.h` | `test/test_button_nodes.cpp` | Button-to-action behavior is well covered. |
| Dome/neck IK and kinematics | Met | `main/include/chopper/dome/*.h`, `main/include/chopper/math/*.h` | `test/test_dome_ik.cpp` | IK/math + node behavior test coverage is strong. |
| Runtime parameter server | Met | `main/include/chopper/core/ParameterServer.h`, `main/chopper/core/ParameterServer.cpp` | `test/test_message_enhancements.cpp`, `test/test_config.cpp` | Declaration/get/set, callbacks, persistence hooks, and validation are implemented. |
| Persistent parameter storage (NVS) | Partial | `loadFromNVS()`/`saveToNVS()` implemented in `main/chopper/core/ParameterServer.cpp` with NVS backend and host fallback | `test/test_config.cpp` (`persistent_parameter_round_trip`, `persistent_load_validation`) | Host behavior verified; target-device NVS behavior still needs HIL confirmation. |
| Serial communication architecture for multi-MCU telemetry | Partial | HAL serial abstractions exist (`ISerialPort`, software/hardware serial wrappers) | HAL and protocol-level tests present for drivers | Cross-device protocol requirements from INSTRUCTIONS are only partly evidenced. |
| WiFi for config + telemetry | Partial | Telemetry and HTTP/WebSocket services implemented in `main/chopper/TelemetryService.cpp`; endpoints available when HTTP server component is present | `test/test_telemetry.cpp` (service behavior) | Runtime endpoint layer is implemented; explicit WiFi deployment/validation remains pending. |
| Web interface for real-time parameter modification | Partial | HTTP parameter API implemented: `GET/POST /api/params` in `main/chopper/TelemetryService.cpp` | No host HTTP integration test yet | API exists; target HTTP runtime validation remains pending. |
| Configurable telemetry outputs | Met | `TelemetryService::Config` supports serial/http/websocket toggles; `TelemetryNode` publishes executor/safety snapshots | `test/test_telemetry.cpp` | Telemetry is emitted over serial and exposed via HTTP/WebSocket endpoints. |
| Configurable logging levels/outputs | Partial | ESP logging macros used broadly | No dedicated tests for output routing | Logging exists but output backends/configurability requirements are not fully evidenced. |
| Build orchestration (Makefile + PlatformIO) | Met | `Makefile`, `platformio.ini` | `make test` host harness active | Build/tooling setup is present and functional for host tests. |
| Cross-platform development compatibility (Windows/Linux/macOS) | Partial | Build scripts/docs + CI workflow `.github/workflows/host-test-matrix.yml` | Host tests run locally; CI workflow prepared | Automated matrix evidence exists as workflow definition; run history pending. |
| Doxygen/code analysis standards | Partial | Added `analyze-cppcheck` target and `.github/workflows/quality-gates.yml` | Local tests pass; static analysis gate defined | Process is in place, but finding baseline trend and Doxygen enforcement remain to be completed. |

## Current Verification Snapshot
- Host test run on 2026-02-28: `make test` -> **12/12 suites passed**.
- Aggregate test cases from output: **272/272 passed**.

## Proposed Expert Panel for Final Quality Evaluation
1. Embedded Robotics Systems Architect (lead)
2. Real-Time/Safety Engineer (ESP32 + FreeRTOS)
3. HAL/Drivers Engineer (UART/ADC/actuator buses)
4. Controls/Mechatronics Engineer (kinematics and motion behavior)
5. Test/Verification Engineer (host + hardware-in-the-loop)
6. C++ Quality/Static Analysis Engineer
7. Product/Requirements Engineer (traceability owner)
