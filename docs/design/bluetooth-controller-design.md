# Bluetooth Multi-Controller System Design

## 1. Overview

This document describes the design for a multi-controller Bluetooth system that allows up to 4 simultaneous controllers to operate an astromech robot, with dynamic role assignment, hot-swap support, and safety-first disconnect behavior.

### 1.1 Current State

The existing implementation in `Controllers.h` uses:
- A flat `std::unordered_map<std::string, ControllerRoles>` mapping MAC addresses to roles at compile time via `SettingsBluetooth.h`
- A monolithic `Controllers` class that holds direct pointers to all actuators (Sabertooth, Maestro, MP3Trigger, etc.) and processes all input in one `processInputs()` method
- `ControllerDecorator` wrapping the raw Bluepad32 `ControllerPtr` with axis normalization, inversion, slew rate limiting, and `ButtonState` tracking
- `ControllerRoles` enum with 4 roles (Drive=1, Dome=3, Animation=7, Camera=15) where values double as player LED bitmasks

The system works but has tight coupling between controller input and actuator output, compile-time-only role assignment, no disconnect safety behavior, and no support for runtime role changes.

### 1.2 Design Goals

1. Decouple controller input from actuator output using the pub/sub message system
2. Support runtime role assignment and switching
3. Handle controller connect/disconnect gracefully with role-specific fallback behavior
4. Retain MAC address whitelisting but make it configurable at runtime via NVS
5. Support controller-specific button mapping profiles
6. Allow input mixing when multiple controllers affect the same subsystem
7. Monitor and report battery levels
8. Remain real-time safe on the critical input path

### 1.3 Constraints

- ESP32 with Bluepad32 library, `CONFIG_BLUEPAD32_MAX_DEVICES=4`
- Bluepad32 Arduino API: `BP32.setup(&onConnect, &onDisconnect)`, `BP32.update()` returns bool, controllers indexed 0-3
- Bluepad32 provides: `battery()` (0=unknown, 1=empty, 255=full), `setPlayerLEDs()`, `setColorLED()`, `playDualRumble()`, `getProperties()` (btaddr, type, vendor_id, product_id, flags)
- Native allowlist support: `uni_bt_allowlist_*` functions with NVS persistence
- Single-core ESP32 execution model with FreeRTOS, 1kHz main loop target
- No dynamic memory allocation on the critical input path

---

## 2. Architecture

### 2.1 Layered Architecture

```
+-----------------------------------------------------------------------+
|                        Application Layer                               |
|  (DriveNode, DomeNode, AnimationNode, CameraNode, SoundNode, etc.)    |
+-----------------------------------------------------------------------+
        ^                    ^                    ^
        | ControllerInput    | RoleAssignment     | ControllerStatus
        | messages           | messages           | messages
+-----------------------------------------------------------------------+
|                     Message Broker (pub/sub)                           |
+-----------------------------------------------------------------------+
        ^                    ^                    ^
        |                    |                    |
+-------------------+  +------------------+  +---------------------+
| ControllerInput   |  | RoleManager      |  | BatteryMonitor      |
| Node              |  | Node             |  | Node                |
| (per-controller)  |  | (singleton)      |  | (singleton)         |
+-------------------+  +------------------+  +---------------------+
        ^                    ^                    ^
        |                    |                    |
+-----------------------------------------------------------------------+
|                   Controller Adapter Layer                              |
|  Bluepad32ControllerSource (implements IControllerSource)              |
+-----------------------------------------------------------------------+
        ^
        |
+-----------------------------------------------------------------------+
|                   Bluepad32 / BT Stack                                 |
+-----------------------------------------------------------------------+
```

### 2.2 Key Classes

```
chopper::core
  IControllerSource          -- abstract interface (already exists)
  PublishingNode             -- base for nodes that pub/sub (already exists)

chopper::adapters
  Bluepad32ControllerSource  -- wraps ControllerDecorator (already exists, needs extension)

chopper::bluetooth (NEW namespace)
  ControllerSlot             -- represents one of 4 physical BT slots
  ControllerInputNode        -- reads from IControllerSource, publishes ControllerInput
  RoleManager                -- assigns/reassigns roles to connected controllers
  RoleAssignmentPolicy       -- strategy for how roles are assigned
  ControllerRegistry         -- tracks all connected controllers and their state
  DisconnectHandler          -- implements role-specific fallback behavior
  InputMixer                 -- merges inputs when multiple controllers share a role
  BatteryMonitorNode         -- periodically publishes battery status
  ButtonMappingProfile       -- per-controller-type button remapping

chopper::messages (extend existing)
  ControllerInput            -- (already exists, needs role_id field)
  RoleAssignment             -- new message for role change events
  ControllerStatus           -- new message for connect/disconnect/battery events
```

### 2.3 Class Diagram

```
                         +---------------------+
                         |  IControllerSource   |
                         |---------------------|
                         | +getControllerData()|
                         | +getConnectionState |
                         | +hasNewData()       |
                         | +getUniqueId()      |
                         | +getCapabilities()  |
                         | +setControllerOutput|
                         +----------^----------+
                                    |
                    +---------------+----------------+
                    |                                 |
      +----------------------------+    +----------------------------+
      | Bluepad32ControllerSource  |    | MockControllerSource       |
      |----------------------------|    | (for testing)              |
      | -controller_: CtlDecorator |    +----------------------------+
      | -initialized_: bool        |
      +----------------------------+
                    |
                    | 1
      +----------------------------+
      |     ControllerSlot         |
      |----------------------------|
      | -slot_index_: uint8_t      |     +---------------------------+
      | -source_: IControllerSource|---->| ControllerInputNode       |
      | -role_: ControllerRole     |     |---------------------------|
      | -state_: SlotState         |     | : PublishingNode           |
      | -mac_address_: MacAddr     |     | -slot_: ControllerSlot*   |
      | -controller_type_: uint16_t|     | -publisher_: TypedPub<CI> |
      | -connect_time_: uint64_t   |     | -profile_: ButtonMapping* |
      | -last_input_time_: uint64_t|     | +process(now)             |
      +----------------------------+     +---------------------------+
                    | *
      +----------------------------+
      |    ControllerRegistry      |
      |----------------------------|     +---------------------------+
      | -slots_[4]: ControllerSlot |     |     RoleManager           |
      | -allowlist_: AllowList     |     |---------------------------|
      | +onConnect(ctl)            |---->| -policy_: RoleAssignPolicy|
      | +onDisconnect(ctl)         |     | -registry_: CtlRegistry*  |
      | +getSlotByRole(role)       |     | +assignRole(slot, role)   |
      | +getSlotByMac(mac)         |     | +reassignRole(slot, role) |
      | +isAllowed(mac)            |     | +onControllerAdded(slot)  |
      +----------------------------+     | +onControllerRemoved(slot)|
                                         +---------------------------+
                                                    |
                                         +---------------------------+
                                         | RoleAssignmentPolicy      |
                                         |   <<interface>>           |
                                         |---------------------------|
                                         | +assignRole(mac, type)    |
                                         +----------^----------------+
                                                    |
                              +---------------------+---------------------+
                              |                                           |
                +----------------------------+          +----------------------------+
                | MacBasedPolicy             |          | FirstAvailablePolicy       |
                | (current behavior)         |          | (auto-assign)              |
                +----------------------------+          +----------------------------+
```

---

## 3. Controller Lifecycle State Machine

### 3.1 Slot States

Each of the 4 Bluepad32 slots has an independent state machine:

```
                         +------------------+
                         |   SLOT_EMPTY     |
                         | (no controller)  |
                         +--------+---------+
                                  |
                          onConnect(ctl)
                          [allowlist check]
                                  |
                     +------------+------------+
                     |                         |
              [allowed]                 [not allowed]
                     |                         |
                     v                         v
           +------------------+      +------------------+
           | SLOT_IDENTIFYING |      | SLOT_REJECTED    |
           | (reading props)  |      | (disconnect ctl) |
           +--------+---------+      +------------------+
                    |                         |
            props available             disconnect sent
                    |                         |
                    v                         v
           +------------------+      +------------------+
           | SLOT_ASSIGNING   |      |   SLOT_EMPTY     |
           | (role lookup)    |      +------------------+
           +--------+---------+
                    |
            role assigned
            LED set
            rumble feedback
                    |
                    v
           +------------------+
           |  SLOT_ACTIVE     |<--------+
           | (producing input)|         |
           +--------+---------+   role reassigned
                    |                   |
           +-------+---------+---------+
           |                 |
     onDisconnect      reassignRole()
           |
           v
  +-------------------+
  | SLOT_DISCONNECTING|
  | (fallback active) |
  +--------+----------+
           |
     fallback complete
     resources released
           |
           v
  +------------------+
  |   SLOT_EMPTY     |
  +------------------+
```

### 3.2 State Transition Table

| Current State       | Event                    | Guard/Condition              | Next State          | Action                                             |
|---------------------|--------------------------|------------------------------|---------------------|-----------------------------------------------------|
| SLOT_EMPTY          | onConnect(ctl)           | allowlist passes             | SLOT_IDENTIFYING    | Read properties, start identify timer                |
| SLOT_EMPTY          | onConnect(ctl)           | allowlist fails              | SLOT_REJECTED       | Log rejection, disconnect controller                 |
| SLOT_REJECTED       | disconnect complete      | --                           | SLOT_EMPTY          | Clear slot data                                      |
| SLOT_IDENTIFYING    | properties available     | --                           | SLOT_ASSIGNING      | Pass to RoleManager                                  |
| SLOT_IDENTIFYING    | identify timeout (2s)    | --                           | SLOT_REJECTED       | Log timeout, disconnect                              |
| SLOT_ASSIGNING      | role assigned            | role available               | SLOT_ACTIVE         | Set player LEDs, rumble, publish RoleAssignment       |
| SLOT_ASSIGNING      | no role available        | --                           | SLOT_ACTIVE         | Assign UNASSIGNED role, set LED pattern              |
| SLOT_ACTIVE         | onDisconnect(ctl)        | --                           | SLOT_DISCONNECTING  | Trigger DisconnectHandler for role                   |
| SLOT_ACTIVE         | reassignRole(newRole)    | new role valid               | SLOT_ACTIVE         | Update role, set LEDs, publish RoleAssignment         |
| SLOT_DISCONNECTING  | fallback applied         | --                           | SLOT_EMPTY          | Release slot, publish ControllerStatus                |

---

## 4. Role Assignment Protocol

### 4.1 Role Definitions

Extend the existing `ControllerRoles` enum:

```cpp
enum class ControllerRole : uint8_t {
    UNASSIGNED = 0,     // Connected but no role
    DRIVE      = 1,     // 0001 - player LED pattern
    DOME       = 3,     // 0011
    ANIMATION  = 7,     // 0111
    CAMERA     = 15,    // 1111

    COUNT      = 5      // For iteration (excluding UNASSIGNED)
};
```

The LED values are retained for backward compatibility with the existing player LED indicator scheme.

### 4.2 Assignment Policies

**MacBasedPolicy** (default, matches current behavior):
```
For each connected controller:
  1. Look up MAC address in the mac-to-role table (NVS-persisted)
  2. If found and role is not already taken -> assign that role
  3. If found but role is taken -> assign UNASSIGNED, log conflict
  4. If not found -> assign UNASSIGNED
```

**FirstAvailablePolicy** (auto-assign for events/demos):
```
For each connected controller:
  1. Check if MAC has a preferred role in NVS
  2. If yes and available -> assign preferred role
  3. If no -> assign first unoccupied role in priority order:
     DRIVE > DOME > ANIMATION > CAMERA
```

**ManualPolicy** (explicit assignment via command):
```
  All controllers connect as UNASSIGNED
  Operator sends RoleAssignment command to assign roles
  Useful for shows where operator pre-stages controllers
```

### 4.3 Runtime Role Switching

Role switching is performed via the `RoleManager`:

```cpp
class RoleManager {
public:
    // Attempt to assign a role to a slot. Returns false if role is occupied.
    bool assignRole(uint8_t slot_index, ControllerRole role);

    // Swap roles between two slots. Atomic operation.
    bool swapRoles(uint8_t slot_a, uint8_t slot_b);

    // Remove role from a slot (sets to UNASSIGNED).
    void unassignRole(uint8_t slot_index);

    // Get which slot holds a given role. Returns -1 if none.
    int8_t getSlotForRole(ControllerRole role) const;

    // Register a policy for automatic assignment.
    void setPolicy(std::unique_ptr<RoleAssignmentPolicy> policy);
};
```

When a role is reassigned:
1. The `RoleManager` publishes a `RoleAssignment` message on topic `chopper/controller/role`
2. The affected `ControllerInputNode` updates its output topic accordingly
3. Player LEDs are updated on both controllers
4. Subscribing nodes (DriveNode, DomeNode, etc.) automatically receive input from the new controller because they subscribe to role-based topics

### 4.4 Topic Naming Convention

```
chopper/controller/0/input      -- raw input from slot 0
chopper/controller/1/input      -- raw input from slot 1
chopper/controller/2/input      -- raw input from slot 2
chopper/controller/3/input      -- raw input from slot 3
chopper/controller/drive/input  -- role-based: whichever controller is DRIVE
chopper/controller/dome/input   -- role-based: whichever controller is DOME
chopper/controller/role         -- role assignment change events
chopper/controller/status       -- connect/disconnect/battery events
```

Application nodes subscribe to **role-based topics** (`chopper/controller/drive/input`), not slot-based topics. The `ControllerInputNode` publishes to both its slot topic and its current role topic.

---

## 5. Disconnect Handling and Safety

### 5.1 Disconnect Detection

Bluepad32 calls `onDisconnectedController` when a controller drops. Additionally, a watchdog monitors `last_input_time_` per slot:

```
If (now - slot.last_input_time > CONTROLLER_TIMEOUT_MS):
    Treat as disconnected even if Bluepad32 hasn't fired callback yet
```

`CONTROLLER_TIMEOUT_MS` default: 200ms (configurable via Executor::Config::emergency_stop_timeout_ms, currently 100ms).

### 5.2 Role-Specific Fallback Behavior

Each role defines its own disconnect policy:

| Role      | Fallback Behavior               | Rationale                                      | Timeout  |
|-----------|----------------------------------|-------------------------------------------------|----------|
| DRIVE     | Immediate safety stop            | Robot must not drift with no driver              | 0ms      |
| DOME      | Hold last position               | Dome drifting is not dangerous                   | 0ms      |
| ANIMATION | Complete current animation, stop  | Finish in-progress sequence gracefully           | 2000ms   |
| CAMERA    | Hold last position               | Camera drift is cosmetic, not dangerous          | 0ms      |

Implementation via a `DisconnectHandler` interface:

```cpp
class DisconnectHandler {
public:
    virtual ~DisconnectHandler() = default;

    // Called immediately on disconnect. Returns fallback message to publish.
    virtual ControllerInput getFallbackInput(ControllerRole role,
                                             const ControllerInput& last_input) = 0;

    // How long to continue publishing fallback before going to zero.
    virtual uint32_t getFallbackDurationMs(ControllerRole role) const = 0;
};

class DefaultDisconnectHandler : public DisconnectHandler {
public:
    ControllerInput getFallbackInput(ControllerRole role,
                                     const ControllerInput& last_input) override {
        ControllerInput fallback{};
        fallback.is_connected = false;

        switch (role) {
            case ControllerRole::DRIVE:
                // All zeros - immediate stop
                break;
            case ControllerRole::DOME:
            case ControllerRole::CAMERA:
                // Hold last dome/camera position (copy relevant axes)
                fallback.axis_rx = last_input.axis_rx;
                fallback.axis_ry = last_input.axis_ry;
                break;
            case ControllerRole::ANIMATION:
                // Copy last state; animation node handles its own timeout
                fallback = last_input;
                fallback.is_connected = false;
                break;
            default:
                break;
        }
        return fallback;
    }

    uint32_t getFallbackDurationMs(ControllerRole role) const override {
        switch (role) {
            case ControllerRole::DRIVE:     return 0;
            case ControllerRole::DOME:      return 0;
            case ControllerRole::ANIMATION: return 2000;
            case ControllerRole::CAMERA:    return 0;
            default:                        return 0;
        }
    }
};
```

### 5.3 Reconnection Behavior

When a controller reconnects (same MAC address):
1. Allowlist check (should pass since it was previously allowed)
2. RoleManager checks if the controller's previous role is still vacant
3. If vacant: auto-reassign same role, resume operation
4. If occupied (another controller took the role): assign UNASSIGNED, notify operator
5. Player LEDs and rumble confirm the assignment

The slot retains a `previous_role_` field for 30 seconds after disconnect to enable seamless reconnection.

---

## 6. Input Pipeline Design

### 6.1 Pipeline Stages

```
Bluepad32 raw data
    |
    v
[1. ControllerDecorator]     -- axis inversion, offset, raw ButtonState tracking
    |
    v
[2. ButtonMappingProfile]    -- remap buttons per controller type
    |
    v
[3. Normalization]           -- raw int32 -> float [-1.0, 1.0]
    |
    v
[4. Deadband]                -- suppress noise around center
    |
    v
[5. SlewRateLimiter]         -- smooth acceleration/deceleration
    |
    v
[6. ControllerInput message] -- published to slot topic + role topic
    |
    v
[7. InputMixer (optional)]   -- merge if multiple controllers share subsystem
    |
    v
[8. Application Node]        -- DriveNode, DomeNode, etc.
```

### 6.2 ControllerInputNode

One instance per active slot. Created when a controller connects, destroyed when it disconnects.

```cpp
class ControllerInputNode : public PublishingNode {
public:
    explicit ControllerInputNode(const std::string& name, ControllerSlot* slot);

    bool initialize() override;
    void process(uint64_t now) override;
    void emergencyStop() override;
    double getUpdateFrequency() const override { return 100.0; } // 100Hz polling

private:
    ControllerSlot* slot_;

    // Publishes to slot-specific topic
    TypedPublisherPtr<messages::ControllerInput> slot_publisher_;

    // Publishes to role-specific topic (updated on role change)
    TypedPublisherPtr<messages::ControllerInput> role_publisher_;

    // Per-controller-type button remapping
    const ButtonMappingProfile* mapping_profile_;

    // Input processing
    SlewRateLimiter slew_x_;
    SlewRateLimiter slew_y_;
    float deadband_;

    // Disconnect watchdog
    uint64_t last_data_time_;
    bool disconnect_fallback_active_;
    uint64_t fallback_start_time_;
};
```

### 6.3 ButtonMappingProfile

Different controller types have different physical layouts. The mapping profile normalizes them:

```cpp
struct ButtonMappingProfile {
    // Human-readable name
    const char* name;          // e.g., "PS5 DualSense", "Switch Pro", "Xbox Series"

    // Controller type ID from Bluepad32
    uint16_t controller_type;  // e.g., CONTROLLER_TYPE_PS5Controller

    // Axis configuration
    int32_t axis_min;          // e.g., -512 for Switch, -32768 for PS/Xbox
    int32_t axis_max;          // e.g., +512 for Switch, +32767 for PS/Xbox

    // Axis inversion defaults (some controllers have inverted Y)
    bool invert_left_y;
    bool invert_right_y;

    // Deadband radius (raw units)
    int32_t deadband;

    // Trigger mode: analog (PS4/PS5/Xbox) vs digital (Switch)
    bool has_analog_triggers;

    // SlewRateLimiter defaults for this controller type
    float default_slew_positive;
    float default_slew_negative;
};

// Built-in profiles
static const ButtonMappingProfile PROFILES[] = {
    {"Switch JoyCon",   CONTROLLER_TYPE_SwitchJoyConLeft,  -512,  512, false, false, 20, false, 0.75f, -0.75f},
    {"Switch JoyCon",   CONTROLLER_TYPE_SwitchJoyConRight, -512,  512, false, false, 20, false, 0.75f, -0.75f},
    {"Switch Pro",      CONTROLLER_TYPE_SwitchProController,-512, 512, false, false, 20, false, 0.75f, -0.75f},
    {"PS4 DualShock",   CONTROLLER_TYPE_PS4Controller,    -512,  512, false, true,  15, true,  0.75f, -0.75f},
    {"PS5 DualSense",   CONTROLLER_TYPE_PS5Controller,    -512,  512, false, true,  15, true,  0.75f, -0.75f},
    {"Xbox One",        CONTROLLER_TYPE_XBoxOneController, -512,  512, false, true,  15, true,  0.75f, -0.75f},
    // Generic fallback
    {"Generic",         CONTROLLER_TYPE_Unknown,           -512,  512, false, false, 20, false, 0.75f, -0.75f},
};
```

Note: Bluepad32 normalizes most controllers to a -512 to 512 range internally. The profile exists primarily for deadband, inversion defaults, and trigger mode differences.

### 6.4 Input Mixing

When multiple controllers need to affect the same subsystem (e.g., dome spin requires R2 from both Drive and Dome controllers, as in the current `processDomeSpin`):

```cpp
class InputMixer {
public:
    enum class MixMode {
        PRIORITY,       // Higher-priority controller wins
        ADDITIVE,       // Sum inputs (clamped)
        AVERAGE,        // Average inputs
        MAX_MAGNITUDE,  // Use whichever input has larger magnitude
    };

    struct MixRule {
        ControllerRole role_a;
        ControllerRole role_b;
        MixMode mode;
        // Which axes/buttons to mix
        uint32_t axis_mask;     // bitmask of axes to apply mixing
        uint32_t button_mask;   // bitmask of buttons to apply mixing (OR logic)
    };

    // Register a mix rule
    void addRule(const MixRule& rule);

    // Apply mixing to produce a combined input for a subsystem
    ControllerInput mix(const ControllerInput& input_a,
                        const ControllerInput& input_b,
                        const MixRule& rule) const;
};
```

The dome spin example maps to:
```cpp
MixRule dome_spin_rule {
    .role_a = ControllerRole::DRIVE,
    .role_b = ControllerRole::DOME,
    .mode = MixMode::PRIORITY,  // DOME controller takes priority
    .axis_mask = 0,             // No axis mixing
    .button_mask = BUTTON_TRIGGER_R,  // Mix R2 button
};
```

---

## 7. MAC Address Allowlist

### 7.1 Current Implementation

Compile-time `#define` in `SettingsBluetooth.h` with a static array `CONTROLLER_MAC_ADDRS[4]`.

### 7.2 Proposed Design

Use Bluepad32's built-in NVS-persisted allowlist (`uni_bt_allowlist_*`) combined with a role-to-MAC mapping stored in ESP32 Preferences:

```cpp
class ControllerAllowlist {
public:
    // Initialize from NVS
    bool initialize();

    // Check if a MAC address is allowed
    bool isAllowed(const uint8_t btaddr[6]) const;

    // Add a MAC address with optional role preference
    bool addAddress(const uint8_t btaddr[6], ControllerRole preferred_role = ControllerRole::UNASSIGNED);

    // Remove a MAC address
    bool removeAddress(const uint8_t btaddr[6]);

    // Get preferred role for a MAC address (returns UNASSIGNED if no preference)
    ControllerRole getPreferredRole(const uint8_t btaddr[6]) const;

    // Set preferred role for a MAC address
    bool setPreferredRole(const uint8_t btaddr[6], ControllerRole role);

    // Enable/disable allowlist enforcement
    void setEnabled(bool enabled);
    bool isEnabled() const;

    // List all entries (for debug/web UI)
    struct Entry {
        uint8_t btaddr[6];
        ControllerRole preferred_role;
    };
    std::vector<Entry> getAllEntries() const;

private:
    // Wraps uni_bt_allowlist_* functions
    // Stores role preferences in ESP32 Preferences namespace "bt_roles"
    Preferences preferences_;
    bool enabled_;
};
```

### 7.3 Migration Path

On first boot after firmware update:
1. Read `CONTROLLER_MAC_ADDRS[]` compile-time values
2. Check if NVS already has entries
3. If NVS is empty: populate from compile-time values
4. If NVS has entries: use NVS values (compile-time values become defaults only)

This allows existing users to maintain their MAC address configuration while enabling runtime changes.

---

## 8. Battery Monitoring

### 8.1 BatteryMonitorNode

```cpp
class BatteryMonitorNode : public PublishingNode {
public:
    explicit BatteryMonitorNode(const std::string& name, ControllerRegistry* registry);

    bool initialize() override;
    void process(uint64_t now) override;
    void emergencyStop() override {}  // Battery monitoring is not safety-critical
    double getUpdateFrequency() const override { return 0.2; }  // Every 5 seconds

private:
    ControllerRegistry* registry_;
    TypedPublisherPtr<messages::ControllerStatus> status_publisher_;

    // Thresholds
    static constexpr uint8_t BATTERY_LOW_THRESHOLD = 51;   // ~20%
    static constexpr uint8_t BATTERY_CRITICAL_THRESHOLD = 13; // ~5%

    // Per-slot tracking to avoid spamming
    uint8_t last_reported_level_[4] = {0};
    bool low_warning_sent_[4] = {false};
    bool critical_warning_sent_[4] = {false};
};
```

### 8.2 Battery Status Message

```cpp
// Extension to ControllerStatus message
class ControllerStatus : public core::TypedMessage<ControllerStatus> {
public:
    enum class EventType : uint8_t {
        CONNECTED,
        DISCONNECTED,
        ROLE_ASSIGNED,
        ROLE_UNASSIGNED,
        BATTERY_UPDATE,
        BATTERY_LOW,
        BATTERY_CRITICAL,
    };

    uint8_t slot_index = 0;
    EventType event = EventType::DISCONNECTED;
    ControllerRole role = ControllerRole::UNASSIGNED;
    uint8_t battery_level = 0;    // 0=unknown, 1=empty, 255=full
    uint16_t controller_type = 0; // Bluepad32 controller type enum
    char mac_address[18] = {0};
};
```

### 8.3 Low Battery Feedback

When battery drops below thresholds:
- **Low (20%)**: Flash controller LED amber, publish `BATTERY_LOW` event
- **Critical (5%)**: Flash controller LED red, publish `BATTERY_CRITICAL` event, audible warning via SoundNode

---

## 9. ControllerRegistry

Central tracking of all 4 controller slots:

```cpp
class ControllerRegistry {
public:
    static constexpr uint8_t MAX_SLOTS = 4;  // Matches CONFIG_BLUEPAD32_MAX_DEVICES

    // Called from Bluepad32 callbacks (must be fast, no blocking)
    void onConnect(ControllerPtr ctl);
    void onDisconnect(ControllerPtr ctl);

    // Slot accessors
    const ControllerSlot& getSlot(uint8_t index) const;
    ControllerSlot& getSlot(uint8_t index);

    // Find slot by various criteria
    int8_t findSlotByMac(const uint8_t btaddr[6]) const;
    int8_t findSlotByRole(ControllerRole role) const;
    int8_t findEmptySlot() const;

    // Get count of active controllers
    uint8_t getActiveCount() const;

    // Iterator for active slots
    template<typename Func>
    void forEachActive(Func&& func) const;

private:
    ControllerSlot slots_[MAX_SLOTS];
    ControllerAllowlist allowlist_;
    RoleManager* role_manager_;  // Set during initialization
};
```

### 9.1 ControllerSlot

```cpp
struct ControllerSlot {
    enum class State : uint8_t {
        EMPTY,
        IDENTIFYING,
        ASSIGNING,
        ACTIVE,
        DISCONNECTING,
        REJECTED,
    };

    // Identity
    uint8_t slot_index;
    uint8_t mac_address[6];
    uint16_t controller_type;      // Bluepad32 controller type
    uint16_t vendor_id;
    uint16_t product_id;

    // State
    State state = State::EMPTY;
    ControllerRole role = ControllerRole::UNASSIGNED;
    ControllerRole previous_role = ControllerRole::UNASSIGNED;

    // Timing
    uint64_t connect_time = 0;
    uint64_t last_input_time = 0;
    uint64_t disconnect_time = 0;

    // Data
    IControllerSource* source = nullptr;
    uint8_t battery_level = 0;

    // Reconnection
    static constexpr uint64_t RECONNECT_ROLE_HOLD_MS = 30000;  // 30s

    bool isActive() const { return state == State::ACTIVE; }
    bool canReassignPreviousRole(uint64_t now) const {
        return previous_role != ControllerRole::UNASSIGNED &&
               (now - disconnect_time) < RECONNECT_ROLE_HOLD_MS;
    }

    void clear() {
        state = State::EMPTY;
        role = ControllerRole::UNASSIGNED;
        source = nullptr;
        battery_level = 0;
        memset(mac_address, 0, 6);
    }
};
```

---

## 10. Integration with Executor

### 10.1 Node Registration

During system startup:

```cpp
void setupControllerSystem(Executor& executor) {
    // Create shared registry and role manager
    auto registry = std::make_shared<ControllerRegistry>();
    auto role_manager = std::make_shared<RoleManager>(registry.get());
    role_manager->setPolicy(std::make_unique<MacBasedPolicy>());

    // Battery monitor node (low frequency)
    auto battery_node = std::make_shared<BatteryMonitorNode>("battery_monitor", registry.get());
    executor.addNode(battery_node);

    // Controller input nodes are created dynamically on connect
    // The registry holds a reference to the executor for this purpose
    registry->setExecutor(&executor);

    // Register Bluepad32 callbacks
    BP32.setup(
        [registry](ControllerPtr ctl) { registry->onConnect(ctl); },
        [registry](ControllerPtr ctl) { registry->onDisconnect(ctl); },
        true  // start scanning
    );
}
```

### 10.2 Dynamic Node Creation

When a controller connects and passes validation:

```cpp
void ControllerRegistry::onConnect(ControllerPtr ctl) {
    // 1. Find empty slot
    int8_t idx = findEmptySlot();
    if (idx < 0) {
        // All slots full - reject
        return;
    }

    // 2. Read MAC address
    auto props = ctl->getProperties();
    memcpy(slots_[idx].mac_address, props.btaddr, 6);

    // 3. Allowlist check
    if (!allowlist_.isAllowed(props.btaddr)) {
        slots_[idx].state = ControllerSlot::State::REJECTED;
        ctl->disconnect();
        slots_[idx].clear();
        return;
    }

    // 4. Create source adapter
    auto decorator = std::make_shared<ControllerDecorator>(ctl);
    auto source = std::make_shared<Bluepad32ControllerSource>(decorator);
    slots_[idx].source = source.get();
    slots_[idx].state = ControllerSlot::State::IDENTIFYING;
    slots_[idx].controller_type = props.type;
    slots_[idx].vendor_id = props.vendor_id;
    slots_[idx].product_id = props.product_id;
    slots_[idx].connect_time = esp_timer_get_time() / 1000;

    // 5. Assign role
    role_manager_->onControllerAdded(idx);

    // 6. Create and register ControllerInputNode
    auto node_name = "controller_input_" + std::to_string(idx);
    auto input_node = std::make_shared<ControllerInputNode>(node_name, &slots_[idx]);
    executor_->addNode(input_node);
    input_node->initialize();
    input_node->activate();

    slots_[idx].state = ControllerSlot::State::ACTIVE;
}
```

---

## 11. Messages (Extended)

### 11.1 Extended ControllerInput

Add role metadata to the existing `ControllerInput` message:

```cpp
class ControllerInput : public core::TypedMessage<ControllerInput> {
public:
    // ... (all existing fields remain unchanged) ...

    // NEW: Role metadata
    ControllerRole role = ControllerRole::UNASSIGNED;
    uint8_t slot_index = 0;
};
```

### 11.2 RoleAssignment Message

```cpp
class RoleAssignment : public core::TypedMessage<RoleAssignment> {
public:
    enum class Action : uint8_t {
        ASSIGNED,       // Role was assigned to a controller
        UNASSIGNED,     // Role was removed from a controller
        SWAPPED,        // Two controllers swapped roles
    };

    Action action = Action::ASSIGNED;
    uint8_t slot_index = 0;
    ControllerRole old_role = ControllerRole::UNASSIGNED;
    ControllerRole new_role = ControllerRole::UNASSIGNED;
    char mac_address[18] = {0};
};
```

---

## 12. Safety Considerations

### 12.1 Real-Time Safety

The input pipeline must be real-time safe:

- **No dynamic allocation** in `ControllerInputNode::process()`. All `ControllerInput` messages use stack allocation or pre-allocated pools.
- **No blocking calls**. Bluepad32's `BP32.update()` is non-blocking. SlewRateLimiter and ButtonState use only arithmetic operations.
- **Bounded execution time**. Each `ControllerInputNode::process()` does a fixed sequence: read source, remap, normalize, slew, publish. Target: < 50us per controller, < 200us total for 4 controllers.

### 12.2 Fail-Safe Defaults

- If no controller is connected for role DRIVE: all motor outputs are zero (enforced by motor safety watchdog in Sabertooth driver).
- If `ControllerInputNode::process()` exceeds its deadline, the Executor's safety check fires `emergencyStop()`.
- The `DisconnectHandler` publishes a zero-input message for DRIVE immediately, ensuring the motor safety system sees a "stop" command rather than no command.

### 12.3 Startup Safety

On system boot:
1. All motor outputs are disabled until at least one controller connects.
2. Bluepad32 scanning is enabled.
3. No role-based topics receive messages until a controller is assigned.
4. Motor safety watchdogs are armed from the start, independent of controller state.

### 12.4 Race Conditions

- `onConnect`/`onDisconnect` callbacks come from the Bluepad32/BTstack thread. The `ControllerRegistry` methods must be thread-safe for these entry points, but the actual node creation happens on the main executor thread via a deferred action queue.
- Role assignment is single-threaded (always on executor thread), so no mutex needed for role state.

```cpp
// Thread-safe connect notification pattern
void ControllerRegistry::onConnect(ControllerPtr ctl) {
    // Queue the connection for processing on the executor thread
    pending_connects_.push(ctl);  // Lock-free queue
}

void ControllerRegistry::processPending() {
    // Called from executor thread only
    ControllerPtr ctl;
    while (pending_connects_.try_pop(ctl)) {
        handleConnect(ctl);  // Safe to modify state here
    }
    while (pending_disconnects_.try_pop(ctl)) {
        handleDisconnect(ctl);
    }
}
```

### 12.5 Emergency Stop Integration

When the Executor triggers a system-wide emergency stop:
1. All `ControllerInputNode` instances publish a zero-input message
2. All SlewRateLimiters are reset to zero
3. Controller LEDs flash red pattern
4. Controllers are NOT disconnected (maintain BT link for recovery)
5. When emergency stop is cleared, controllers resume from zero (not from last position)

---

## 13. Configuration via NVS

### 13.1 Stored Parameters

| NVS Namespace | Key Pattern              | Type     | Description                                |
|---------------|--------------------------|----------|--------------------------------------------|
| bt_allow      | enabled                  | bool     | Allowlist enforcement on/off               |
| bt_allow      | addr_0 ... addr_N        | blob(6)  | Allowed MAC addresses                      |
| bt_roles      | role_XX:XX:XX:XX:XX:XX   | uint8_t  | Preferred role for MAC address             |
| bt_config     | policy                   | uint8_t  | Assignment policy (0=Mac, 1=Auto, 2=Manual)|
| bt_config     | reconnect_hold_ms        | uint32_t | How long to hold role for reconnecting ctl |
| bt_config     | controller_timeout_ms    | uint32_t | Disconnect detection timeout               |

### 13.2 Web Interface Integration

The configuration parameters above should be exposed via the future web interface (Phase 4 of the development plan). The NVS backend ensures settings persist across reboots without recompilation.

---

## 14. Implementation Priority

### Phase 1: Core Infrastructure
1. `ControllerSlot` struct and `ControllerRegistry` (replaces `Controllers` class state management)
2. `ControllerInputNode` (replaces monolithic `processInputs()`)
3. `MacBasedPolicy` (replicates current behavior)
4. Extended `ControllerInput` message with role field

### Phase 2: Safety and Disconnect
5. `DisconnectHandler` with role-specific fallback
6. Watchdog-based disconnect detection
7. Emergency stop integration

### Phase 3: Runtime Features
8. `RoleManager` with runtime assignment/swapping
9. `ControllerAllowlist` with NVS persistence
10. `BatteryMonitorNode`

### Phase 4: Advanced Features
11. `ButtonMappingProfile` per controller type
12. `InputMixer` for shared-subsystem scenarios
13. `FirstAvailablePolicy` and `ManualPolicy`
14. Web UI for controller configuration
