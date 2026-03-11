# Chopper Registry

> Source of truth for topics, nodes, and data flow. Referenced by `CLAUDE.md`.
> When you add/remove/rename a topic, node, or message type, update this file in the same commit.

## Topic Registry

Canonical topic names used across the codebase. **Always reuse these** -- do not invent new topic names without checking here first.

| Topic | Message Type | Publisher(s) | Subscriber(s) |
|-------|-------------|-------------|----------------|
| `controller/drive` | ControllerInput | BluepadInputNode | DriveNode |
| `controller/dome` | ControllerInput | BluepadInputNode | DomeNode, DomeArmsNode, PeriscopeNode |
| `controller/animation` | ControllerInput | BluepadInputNode | SoundNode, BodyUtilityNode |
| `controller/camera` | ControllerInput | BluepadInputNode | (reserved) |
| `drive/cmd` | MotorCommand | DriveNode | MotorBridgeNode(s) |
| `dome/motor/cmd` | MotorCommand | DomeNode | MotorBridgeNode |
| `servo/cmd` | ServoCommand | (aggregate command channel; application default) | TelemetryIOTapNode |
| `dome/position` | SensorData | DomeNode | (telemetry) |
| `servo/body/cmd` | ServoCommand | BodyUtilityNode | ServoBridgeNode |
| `servo/dome/cmd` | ServoCommand | DomeArmsNode, PeriscopeNode, NeckNode | ServoBridgeNode |
| `led/cmd` | LedCommand | (reserved / aggregate LED command channel) | TelemetryIOTapNode |
| `led/front/cmd` | LedCommand | (reserved) | TelemetryIOTapNode |
| `led/back/cmd` | LedCommand | (reserved) | TelemetryIOTapNode |
| `audio/cmd` | AudioCommand | SoundNode, DriveNode | AudioBridgeNode |
| `system/status` | SystemStatus | SafetyNode | (telemetry) |

TelemetryIOTapNode subscribes to all of the above for passthrough to the telemetry UI, including aggregate `servo/cmd` and LED command channels reserved for tap/bridge visibility.

## Node Inventory

| Node | Header | Role |
|------|--------|------|
| BluepadInputNode | `nodes/BluepadInputNode.h` | BT controller -> per-role ControllerInput topics |
| DriveNode | `nodes/DriveNode.h` | Controller input -> tank drive MotorCommands |
| DomeNode | `nodes/DomeNode.h` | Controller input -> dome rotation MotorCommand |
| NeckNode | `nodes/NeckNode.h` | Controller input -> 3-RSS neck IK -> ServoCommands |
| DomeArmsNode | `nodes/DomeArmsNode.h` | Controller input -> dome arm ServoCommands |
| PeriscopeNode | `nodes/PeriscopeNode.h` | Controller input -> periscope ServoCommands |
| SoundNode | `nodes/SoundNode.h` | Controller input -> AudioCommands |
| BodyUtilityNode | `nodes/BodyUtilityNode.h` | Controller input -> body utility arm ServoCommands |
| MotorBridgeNode | `nodes/MotorBridgeNode.h` | MotorCommand -> IMotorDriver (one per motor) |
| ServoBridgeNode | `nodes/ServoBridgeNode.h` | ServoCommand -> IServoController |
| AudioBridgeNode | `nodes/AudioBridgeNode.h` | AudioCommand -> IAudioDriver |
| SafetyNode | `nodes/SafetyNode.h` | Publishes SystemStatus, monitors safety state |
| TelemetryNode | `nodes/TelemetryNode.h` | Periodic telemetry frame emission |
| TelemetryIOTapNode | `nodes/TelemetryIOTapNode.h` | Passthrough tap on all topics for telemetry |
| DriverUpdateNode | `nodes/DriverUpdateNode.h` | Periodic driver tick (serial flush, etc.) |

## System Data Flow

```
BT Controllers (Core 0, Bluepad32)
  +-- BluepadInputNode
        +-- controller/drive --> DriveNode --> drive/cmd --> MotorBridgeNode --> Sabertooth
        +-- controller/dome  --> DomeNode  --> dome/motor/cmd --> MotorBridgeNode --> SyRen
        |                    +-- NeckNode  --> servo/dome/cmd --> ServoBridgeNode --> Maestro(dome)
        |                    +-- DomeArmsNode --> servo/dome/cmd -->  ^
        |                    +-- PeriscopeNode --> servo/dome/cmd --> ^
        +-- controller/animation --> SoundNode --> audio/cmd --> AudioBridgeNode --> MP3 Trigger
        |                        +-- BodyUtilityNode --> servo/body/cmd --> ServoBridgeNode --> Maestro(body)
        +-- controller/camera  (reserved)

SafetyNode --> system/status
TelemetryIOTapNode (taps all topics) --> TelemetryService --> serial TEL: lines --> UI bridge
```
