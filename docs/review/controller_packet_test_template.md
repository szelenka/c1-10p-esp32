# Controller Packet Test Intake Template

Date:
Owner:
Robot name:
Firmware branch/commit:

## Purpose
Use this template to describe controller packet scenarios we need to test end-to-end:

`chopper_on_controller_data` -> `chopper_bt_get_gamepad` -> `BluepadInputNode` -> behavior node(s) -> output message topic(s)

Once this is filled in, we can implement deterministic host/integration tests that assert published messages.

This template is intent-first: scenarios should describe user intent (for example, `PERISCOPE_UP`) rather than physical button labels (`A`, `X`, etc.).

## 1) Runtime Configuration Snapshot
Fill this with the exact values used on your robot when you observed the behavior.

- `config::bluetooth::DRIVE_MAC`:
- `config::bluetooth::DOME_MAC`:
- `config::bluetooth::ANIMATE_MAC`:
- `config::bluetooth::CAMERA_MAC`:
- `drive.system` (0=Arcade, 1=Curve, 2=Tank, 3=ReelTwo):
- `drive.deadband`:
- `drive.max_speed`:
- `drive.speed_boost`:
- `ctrl.drive.slew_rate`:
- `dome.max_speed`:
- `dome.deadband`:
- `dome.motor_inverted`:
- `dome.spin_slew_rate`:
- Any non-default node wiring/topics:

## 2) Packet Format Notes
Use raw values as they enter `chopper_on_controller_data` (`uni_controller_t.gamepad` style values).

- Axes range expected by your controller (usually `-512..512`):
- Trigger/brake range:
- Button mask reference used:
- Misc button mask reference used:
- D-pad encoding used:

## 3) Intent To Physical Mapping (Required)
Fill this table using the controller profile you are testing. This is the only place physical button names should appear.

| Intent | Controller role | Physical control on this controller | Raw packet field(s) expected |
|---|---|---|---|
| `PERISCOPE_UP` | `DRIVE` |  |  |
| `PERISCOPE_DOWN` | `DRIVE` |  |  |
| `PERISCOPE_SPIN_LEFT` | `DRIVE` |  |  |
| `PERISCOPE_SPIN_RIGHT` | `DRIVE` |  |  |
| `DOME_DOORS_TOGGLE` | `DRIVE` |  |  |
| `BODY_UTILITY_TOGGLE` | `DRIVE` |  |  |
| `CARPET_MODE_TOGGLE` | `DRIVE` |  |  |

## 4) Scenario Template (Copy Per Test Case)
Create one section per scenario. Keep each scenario focused on one behavior.

### Scenario ID: `SCENARIO_NAME`
- Goal:
- User intent (from mapping table above):
- Controller role under test (`DRIVE|DOME|ANIMATION|CAMERA`):
- Expected primary output topic(s) (examples: `drive/cmd`, `dome/motor/cmd`, `servo/cmd`, `audio/cmd`):
- Required preconditions:
- Allowed tolerance (for float values):
- Expected message count/order constraints:
- Completion rule (how we know action finished: time-based / sensor-based / limit-switch / N/A):

#### 4.1 Input Packet Sequence
List packets in order. `dt_ms` is time since previous packet.

| Step | dt_ms | bt_slot | connected | mac | controller_type | battery | axis_x | axis_y | axis_rx | axis_ry | brake | throttle | buttons_hex | misc_hex | dpad_hex | notes |
|---|---:|---:|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---|---|---|---|
| 1 | 0 | 0 | true | `AA:BB:CC:DD:EE:FF` | 0 | 255 | 0 | 0 | 0 | 0 | 0 | 0 | `0x0000` | `0x00` | `0x00` | baseline |
| 2 | 20 | 0 | true | `AA:BB:CC:DD:EE:FF` | 0 | 255 | 0 | 0 | 0 | 0 | 0 | 0 | `<from intent mapping>` | `0x00` | `0x00` | trigger intent |

#### 4.2 Expected Published Messages
List all expected outputs that should be observed from node topics after the sequence.

| Topic | Message type | Required fields | Forbidden fields/values | Count |
|---|---|---|---|---:|
| `controller/drive` | `ControllerInput` | `is_connected=true`, normalized axis values match packet | wrong MAC/role mapping | 1+ |
| `drive/cmd` | `MotorCommand` | `motor_id=0,1`; `command_type=SET_SPEED`; value in expected range | `EMERGENCY_STOP` for this scenario | 2 |

#### 4.3 Assertions
Use explicit assertions we should codify in tests.

1.  
2.  
3.  

#### 4.4 Out-of-Scope For This Scenario
List what should not be asserted here (to keep tests stable and focused).

-  

## 5) High-Value Scenario Checklist
Mark scenarios you can provide packet evidence for.

- [ ] Neutral packet publishes zero-output drive command.
- [ ] Forward drive stick generates two `drive/cmd` messages with expected sign/magnitude.
- [ ] Turn input produces left/right asymmetry.
- [ ] Dome stick input generates expected `dome/motor/cmd` speed.
- [ ] Button action emits expected `audio/cmd` track.
- [ ] Button action emits expected `servo/cmd` command(s).
- [ ] Disconnect transition produces expected fallback/zero behavior.
- [ ] Reconnect with same MAC restores expected role/topic path.

## 6) Evidence Attachments
Provide whatever data you have to reduce ambiguity.

- Serial logs showing packet and output message traces:
- Video timestamp references (if behavior is visual):
- Controller model + firmware:
- Any known quirks (deadzone, inverted axis, drift):

## 7) Test Suite Split
Use two suites so intent behavior and physical mapping are independently testable.

- Suite A: `packet_to_action` (intent behavior)
- Input defined by intent names (no physical button names in assertions).
- Verifies output topics/messages and end-state behavior.

- Suite B: `control_mapping` (controller/profile mapping)
- Verifies that each intent maps to the expected raw packet/button fields for a given controller profile.
- Can vary by controller type without changing Suite A behavior assertions.

## 8) Delivery Format
Send back a completed file named:

`docs/review/controller_packet_test_cases_<date>.md`

Use exact numeric values where possible. If unknown, write `UNKNOWN` instead of leaving blank.
