# Behaviors (what the robot does, for human-to-agent translation)

> Extracted from CLAUDE.md. Referenced on demand, not loaded on every task.

Each behavior maps a controller action to a robot response. All button-triggered behaviors
go through the **intent layer** (`DriveIntentMapping.h`), so remapping a button only requires
editing the mapping table — not the node that performs the action.

**Exception**: face tracking toggle requires a stateful 2-second hold, so it is handled directly
by `BluepadInputNode` rather than through the standard intent mapping table.

**Drive controller** (`controller/drive` topic):
- Left stick → tank/arcade drive → `DriveNode` → Sabertooth motors
- Left thumb press → toggle carpet mode (speed boost) → `DriveNode`
- X button → raise/lower periscope → `PeriscopeNode` → dome Maestro
- A button → spin periscope left → `PeriscopeNode` → dome Maestro
- Y button → spin periscope right → `PeriscopeNode` → dome Maestro
- Select button → toggle dome doors → `DomeArmsNode` → dome Maestro
- B button (hold) → extend body utility arm → `BodyUtilityNode` → body Maestro

**Dome controller** (`controller/dome` topic):
- Left stick X → spin dome → `DomeNode` → SyRen motor
- Right stick X/Y → tilt neck (3-RSS IK) → `NeckNode` → body Maestro
- Left thumb → toggle neck enabled → `NeckNode`
- L1 / R1 → lower / raise neck height → `NeckNode`
- SL+SR hold (2s) → toggle face tracking → `DomeNode` → `OpenMvBridgeNode` → OpenMV (handled by `BluepadInputNode`, not in intent map)
- Select button → toggle random dome roam → `DomeNode`
- R2 button → toggle eye color (red/blue) → `DomeNode` → `OpenMvBridgeNode` → OpenMV
- A button → play sound A → `SoundNode` → MP3 Trigger
- B button → play sound B → `SoundNode` → MP3 Trigger
- Start button → play random sound → `SoundNode` → MP3 Trigger

**To change what a button does**: edit the intent map in `main/include/chopper/input/DriveIntentMapping.h` (`DriveIntentMap` for drive controller, `DomeIntentMap` for dome controller).

**To change what an action does**: edit the corresponding node in `main/include/chopper/nodes/`.

**To add a new button-triggered action**: add a `UserIntent` enum value, a field on `ControllerInput`, a mapping entry, and implement the behavior in a node. Follow the "Add a node" golden task if it needs a new node.
