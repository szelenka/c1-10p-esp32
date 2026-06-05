# Chopper Registry

> Source of truth for topics, nodes, and data flow. Referenced by `CLAUDE.md`.
> When you add/remove/rename a topic, node, or message type, update this file in the same commit.

## Topic Registry

Canonical topic names used across the codebase. **Always reuse these** -- do not invent new topic names without checking here first.

| Topic | Message Type | Publisher(s) | Subscriber(s) |
|-------|-------------|-------------|----------------|
| `controller/drive` | ControllerInput | BluepadInputNode | DriveNode, DomeNode, DomeArmsNode, PeriscopeNode, BodyUtilityNode, BodyDoorsNode, SoundNode |
| `controller/dome` | ControllerInput | BluepadInputNode | DomeNode, NeckNode, SoundNode |
| `controller/animation` | ControllerInput | BluepadInputNode | (reserved) |
| `controller/camera` | ControllerInput | BluepadInputNode | (reserved) |
| `drive/cmd` | MotorCommand | DriveNode | MotorBridgeNode(s) |
| `dome/motor/cmd` | MotorCommand | DomeNode | MotorBridgeNode |
| `dome/position` | SensorData | DomeNode | (telemetry) |
| `servo/cmd` | ServoCommand | (aggregate command channel; application default) | TelemetryIOTapNode |
| `servo/body/move` | ServoCommand | BodyUtilityNode, BodyDoorsNode | ServoMotionNode |
| `servo/body/cmd` | ServoCommand | ServoMotionNode, NeckNode | ServoBridgeNode |
| `servo/dome/move` | ServoCommand | DomeArmsNode, PeriscopeNode | ServoMotionNode |
| `servo/dome/cmd` | ServoCommand | ServoMotionNode | ServoBridgeNode |
| `audio/cmd` | AudioCommand | SoundNode, DriveNode | AudioBridgeNode |
| `led/cmd` | LEDCommand | (reserved / aggregate LED command channel) | TelemetryIOTapNode |
| `led/front/cmd` | LEDCommand | BodyLedNode | TelemetryIOTapNode |
| `led/back/cmd` | LEDCommand | (reserved) | TelemetryIOTapNode |
| `led/dome_eye/cmd` | LEDCommand | DomeNode, PeriscopeNode | OpenMvBridgeNode, PeriscopeNode |
| `openmv/tracking/cmd` | TrackingCommand | DomeNode | OpenMvBridgeNode |
| `vision/result` | VisionResult | OpenMvBridgeNode | DomeNode |
| `system/status` | SystemStatus | SafetyNode | (telemetry) |

TelemetryIOTapNode subscribes to all command/status topics for passthrough to the telemetry UI, including aggregate `servo/cmd` and LED command channels reserved for tap/bridge visibility.

## Node Inventory

| Node | Header | Stage | Single Responsibility |
|------|--------|-------|----------------------|
| BluepadInputNode | `nodes/BluepadInputNode.h` | Input | Read BT controller state, apply intent mapping, publish per-role ControllerInput. All button/axis interpretation happens here. |
| DriveNode | `nodes/DriveNode.h` | Action | Translate drive intents into tank-drive MotorCommands. Never inspects raw buttons. |
| DomeNode | `nodes/DomeNode.h` | Action | Translate dome intents into dome rotation MotorCommand, eye LEDs, tracking commands. Subscribes to both `controller/dome` and `controller/drive` (cross-controller dome spin). Never inspects raw buttons. |
| NeckNode | `nodes/NeckNode.h` | Action | Translate neck intents into 3-RSS IK ServoCommands. Never inspects raw buttons. |
| DomeArmsNode | `nodes/DomeArmsNode.h` | Action | Translate dome-arm intents into ServoCommands. Never inspects raw buttons. |
| PeriscopeNode | `nodes/PeriscopeNode.h` | Action | Translate periscope intents into ServoCommands and periscope LEDCommands. Never inspects raw buttons. |
| SoundNode | `nodes/SoundNode.h` | Action | Translate sound intents into AudioCommands. Never inspects raw buttons. |
| BodyUtilityNode | `nodes/BodyUtilityNode.h` | Action | Translate body utility intents into ServoCommands. Never inspects raw buttons. |
| BodyDoorsNode | `nodes/BodyDoorsNode.h` | Action | Translate body-door toggle intents into timed ServoCommands. Never inspects raw buttons. |
| MotorBridgeNode | `nodes/MotorBridgeNode.h` | Bridge | Safety-gated forwarding of MotorCommand to IMotorDriver. No decision logic. |
| ServoMotionNode | `nodes/ServoMotionNode.h` | Action | Convert timed servo motion requests into immediate ServoCommands. No driver I/O. |
| ServoBridgeNode | `nodes/ServoBridgeNode.h` | Bridge | Safety-gated forwarding of ServoCommand to IServoController. No decision logic. |
| AudioBridgeNode | `nodes/AudioBridgeNode.h` | Bridge | Forward AudioCommand to IAudioDriver. No decision logic. |
| OpenMvBridgeNode | `nodes/OpenMvBridgeNode.h` | Bridge | ESP32 ↔ OpenMV serial bridge for OpenMV LEDs and vision tracking. Subscribes to `led/dome_eye/cmd` and `openmv/tracking/cmd`; publishes `vision/result`. |
| SafetyNode | `nodes/SafetyNode.h` | Safety | Publish SystemStatus, monitor safety state. Does not issue commands. |
| TelemetryNode | `nodes/TelemetryNode.h` | Telemetry | Periodic telemetry frame emission. Read-only observer. |
| TelemetryIOTapNode | `nodes/TelemetryIOTapNode.h` | Telemetry | Passthrough tap on all topics for telemetry. Read-only observer. |
| BodyLedNode | `nodes/BodyLedNode.h` | Action | Autonomous triangle-wave fade on body LED. Publishes LEDCommand for telemetry visibility. |
| DriverUpdateNode | `nodes/DriverUpdateNode.h` | Driver | Periodic driver tick (serial flush, etc.). No command logic. |

## System Data Flow

```
BT Controllers (Core 0, Bluepad32)
  +-- BluepadInputNode
        +-- controller/drive --> DriveNode --> drive/cmd --> MotorBridgeNode --> Sabertooth
        |                    +-- DomeNode (cross-controller dome spin via L2)
        |                    +-- DomeArmsNode --> servo/dome/move --> ServoMotionNode --> servo/dome/cmd --> ServoBridgeNode --> Maestro(dome)
        |                    +-- PeriscopeNode --> servo/dome/move --> ^
        |                    |                \-> led/dome_eye/cmd --> OpenMvBridgeNode --> OpenMV serial
        |                    +-- BodyUtilityNode --> servo/body/move --> ServoMotionNode --> servo/body/cmd --> ServoBridgeNode --> Maestro(body)
        |                    +-- BodyDoorsNode --> servo/body/move --> ^
        +-- controller/dome  --> DomeNode  --> dome/motor/cmd --> MotorBridgeNode --> SyRen
        |                    |            --> dome/position (sensor data)
        |                    |            --> led/dome_eye/cmd --> OpenMvBridgeNode --> OpenMV serial
        |                    |            --> openmv/tracking/cmd --> OpenMvBridgeNode --> OpenMV serial
        |                    +-- NeckNode  --> servo/body/cmd --> ServoBridgeNode --> Maestro(body)
        |                    +-- SoundNode --> audio/cmd --> AudioBridgeNode --> MP3 Trigger
        +-- controller/animation  (reserved)
        +-- controller/camera     (reserved)

OpenMvBridgeNode --> vision/result --> DomeNode (face tracking closed loop)

SafetyNode --> system/status
TelemetryIOTapNode (taps all topics) --> TelemetryService --> serial TEL: lines --> UI bridge
```
