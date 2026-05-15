# System Architecture and Integration Plan

**Date**: 2026-02-27
**Status**: Design
**Input documents**:
- `core-framework-audit.md` -- Core framework analysis
- `message-system-design.md` -- Message/topic system
- `safety-realtime-design.md` -- Safety and real-time guarantees
- `hal-design.md` -- Hardware abstraction layer
- `bluetooth-controller-design.md` -- Multi-controller Bluetooth system
- `cross-cutting-concerns.md` -- Cross-cutting analysis and conflict resolution

---

## 1. System Block Diagram

```
+===========================================================================+
|                        CHOPPER SYSTEM ARCHITECTURE                        |
+===========================================================================+

  CORE 0 (Protocol)                    CORE 1 (Application)
  +-----------------------+            +-----------------------------------+
  |                       |            |                                   |
  |  BT/WiFi Stack        |            |         EXECUTOR (1 kHz)          |
  |  (managed by ESP-IDF) |            |  +-------------------------------+|
  |                       |            |  | Phase 0: HW Watchdog Feed     ||
  |  Bluepad32 Library    |   ISR/     |  | Phase 1: Safety Checks        ||
  |    BP32.update()      |---queue--->|  | Phase 2: Message Dispatch     ||
  |    onConnect()        |            |  | Phase 3: Node Execution       ||
  |    onDisconnect()     |            |  | Phase 4: Statistics            ||
  |                       |            |  | Phase 5: Sleep (vTaskDelay)   ||
  +-----------+-----------+            |  +-------------------------------+|
              |                        |           |                       |
              |  Lock-free             |           v                       |
              |  connect/              |  +-------------------------------+|
              |  disconnect            |  |       NODE GRAPH               ||
              |  queue                 |  |                                ||
              v                        |  |  [Safety]  [Controller]        ||
  +-----------------------+            |  |  [Drive]   [Dome]              ||
  |  ControllerRegistry   |            |  |  [Servo]   [Audio]            ||
  |  (connect/disconnect  |            |  |  [LED]     [Telemetry]        ||
  |   pending queues)     |            |  |  [Battery] [Sensor]           ||
  +-----------------------+            |  +-------------------------------+|
                                       |           |                       |
                                       |           v                       |
                                       |  +-------------------------------+|
                                       |  |     MESSAGE BROKER             ||
                                       |  |  TopicRegistry (32 topics)    ||
                                       |  |  MessagePools (per-type)      ||
                                       |  |  PriorityDispatcher           ||
                                       |  |  ParameterServer              ||
                                       |  +-------------------------------+|
                                       |           |                       |
                                       |           v                       |
                                       |  +-------------------------------+|
                                       |  |     HAL DRIVER MANAGER         ||
                                       |  |  UartBusManager               ||
                                       |  |  SabertoothBusDriver          ||
                                       |  |  MaestroServoDriver (x2)      ||
                                       |  |  MP3TriggerDriver             ||
                                       |  |  AnalogSensorDriver           ||
                                       |  |  GpioDriver (LEDs)            ||
                                       |  +-------------------------------+|
                                       +-----------------------------------+
                                                    |
                                                    v
                                       +-----------------------------------+
                                       |     PHYSICAL HARDWARE              |
                                       |  Sabertooth 2x32  |  SyRen 10    |
                                       |  Maestro Body     |  Maestro Dome |
                                       |  MP3 Trigger      |  OpenMV       |
                                       |  Dome Pot (ADC)   |  LEDs (GPIO)  |
                                       +-----------------------------------+
```

---

## 2. Data Flow Diagram

### 2.1 Controller Input to Actuator Output

```
BT Controller (physical)
    |
    | Bluepad32 / btstack (Core 0)
    v
BP32.update() -----> ControllerPtr data available
    |
    | Lock-free queue (ISR-safe)
    v
ControllerRegistry::processPending()       [Executor Phase 3, Core 1]
    |
    v
ControllerInputNode::process()
    | 1. Read IControllerSource
    | 2. Apply ButtonMappingProfile
    | 3. Normalize axes to [-1.0, 1.0]
    | 4. Apply deadband filter
    | 5. Apply SlewRateLimiter
    | 6. Stamp with role and slot index
    v
publish(ControllerInput) ---> slot topic:  controller/0/input
                          \-> role topic:  controller/drive/input
    |
    | MessageBroker: zero-copy pool delivery
    v
DriveControlNode::onControllerInput()
    | 1. Read ParameterServer (drive.max_speed, drive.deadband)
    | 2. Map joystick axes to motor speeds
    | 3. Apply differential mixing (arcade/tank)
    v
publish(MotorCommand) ---> drive/cmd topic
    |
    v
MotorDriverNode::onMotorCommand()              [HAL Bridge Node]
    | 1. Map normalized speed to driver range
    | 2. Call IMotorDriver::set(speed)
    | 3. Feed MotorSafetyMonitor
    v
SabertoothMotorDriver::set()
    | Packet Serial TX via SoftwareSerial
    v
Sabertooth 2x32 (physical motor controller)
    |
    v
DC Motors (wheels spin)
```

### 2.2 Parallel Subsystem Data Flows

```
ControllerInput (DOME role) --> DomeControlNode --> DomeMotorDriverNode --> SyRen 10
                                    |
                                    +--> DomeSensorNode (reads potentiometer)
                                    |    publishes SensorData
                                    |
                                    +--> (future: closed-loop position control)

ControllerInput (ANIMATION role) --> AnimationNode --> ServoDriverNode --> Maestro (body)
                                                   \-> ServoDriverNode --> Maestro (dome)
                                                   \-> AudioDriverNode --> MP3 Trigger

ControllerInput (CAMERA role) --> CameraNode --> ServoDriverNode --> Maestro (dome, pan/tilt)

SafetyMonitorNode --> reads MotorSafetyMonitor, ConnectionMonitor, HeapMonitor, StackMonitor
                  --> publishes SystemStatus
                  --> triggers degradation mode transitions

BatteryMonitorNode --> reads ControllerRegistry battery levels
                   --> publishes ControllerStatus
```

---

## 3. Thread/Task and Interrupt Model

### 3.1 FreeRTOS Task Assignment

Runtime source of truth: `main/main.c`, `main/chopper/RuntimeMain.cpp`, and generated `sdkconfig`.

| Task / ISR | Core | Priority | Stack Size | Purpose |
|------------|------|----------|------------|---------|
| `app_main` / `btstack_run_loop_execute()` | 0 | ESP-IDF main task default | `CONFIG_ESP_MAIN_TASK_STACK_SIZE` | Startup, Bluepad32 setup, BTstack run loop |
| BT controller | 0 | ESP-IDF managed | ESP-IDF managed | Bluetooth Classic + BLE controller (`CONFIG_BTDM_CTRL_PINNED_TO_CORE_0`) |
| `chopper_exec` | 1 | `configMAX_PRIORITIES - 1` | 8192 B | Executor: all node processing, safety, message dispatch, driver calls |
| `telemetry_pub` | 0 | telemetry config | 4096 B default | Async serial telemetry publishing |
| GPIO ISR service / SoftwareSerial RX | 0 expected | ESP-IDF interrupt allocator | ISR stack | SoftwareSerial start-bit interrupt and RX bit sampling |
| SoftwareSerial TX writes | caller core, normally 1 | caller task priority | caller task stack | Blocking bit-banged UART TX for actuator/audio drivers |
| `wifi` (optional) | 0 | `tskIDLE_PRIORITY + 2` | 4096 B | WiFi stack for web dashboard (only when enabled) |
| `httpd` (optional) | 0 | `tskIDLE_PRIORITY + 1` | 8192 B | Web server for configuration/telemetry |
| `idle0` | 0 | 0 | minimal | FreeRTOS idle (feeds TWDT) |
| `idle1` | 1 | 0 | minimal | FreeRTOS idle |

SoftwareSerial RX core placement depends on where `gpio_install_isr_service(0)` first succeeds. In this runtime, SoftwareSerial `begin()` happens during `app_main` startup before the executor is created, so the GPIO ISR service is expected on CPU0. If another component installs the GPIO ISR service earlier from a different core, per-pin SoftwareSerial handlers will attach to that existing service instead.

### 3.2 Core Affinity Rationale

- **Core 0**: Wireless protocol stacks, Bluepad32/BTstack run loop, telemetry publishing, and the expected SoftwareSerial RX GPIO ISR service.
- **Core 1**: Application executor, bridge nodes, driver calls, and blocking SoftwareSerial TX writes for motor, servo, and MP3 commands.
- **Cross-core boundary**: ControllerRegistry state crosses from BT callbacks on CPU0 to `BluepadInputNode` on CPU1. Telemetry observations are made from CPU1 node callbacks and published by `telemetry_pub` on CPU0.

### 3.3 Synchronization Points

| Shared Resource | Core 0 Access | Core 1 Access | Mechanism |
|----------------|---------------|---------------|-----------|
| Controller connect/disconnect events | Write (BT callback) | Read (executor) | Lock-free SPSC ring |
| Controller input data | Write (BP32.update) | Read (ControllerInputNode) | Atomic flag + copy |
| ParameterServer (if web UI writes) | Write (HTTP task) | Read (nodes) | Atomic generation counter |
| Statistics (if web UI reads) | Read (HTTP task) | Write (executor) | Double-buffer swap |
| NVS writes | Write (any task) | Write (any task) | NVS internal mutex (ESP-IDF managed) |

---

## 4. Memory Budget Allocation

### 4.1 SRAM Budget (320 KB total, ~280 KB usable after IRAM/DRAM split)

| Category | Allocation | Notes |
|----------|-----------|-------|
| **ESP-IDF System** | | |
| FreeRTOS kernel | 12 KB | TCBs, queues, timers |
| Bluetooth Classic stack | 55 KB | btstack + Bluepad32 |
| System overhead (heap metadata, IPC) | 15 KB | |
| **Task Stacks** | | |
| Executor (main) | 8 KB | Runs all nodes |
| BT stack task | 4 KB | Managed by ESP-IDF |
| **Framework** | | |
| Message system | 13.8 KB | Pools, queues, topics, params, timers, services |
| Safety system | 6.2 KB | Motor safety, health, timing, error log, monitors |
| HAL drivers | 8 KB | All drivers + manager (PenumbraComm reduced to 1.5 KB) |
| Core executor/nodes | 6 KB | Node instances, vtables, names |
| Bluetooth controller system | 3 KB | Registry, slots, role manager, allowlist |
| **Subtotal: Fixed** | **~131 KB** | |
| **Available for application** | **~149 KB** | |

### 4.2 Budget Notes

- WiFi adds ~45 KB if enabled; reduces available to ~104 KB. WiFi should be an on-demand feature, not always-on.
- The PenumbraCommDriver buffer has been reduced from 7.5 KB to ~1.5 KB per cross-cutting recommendations (BUFFER_SIZE: 25->8, BUFFER_DATA_MAX_SIZE: 256->64).
- The core framework 10 KB estimate from the audit overlaps with the message system 13.8 KB estimate. After deduplication the combined cost is ~20 KB (message system replaces existing broker).
- Application logic (animations, sequences, special behaviors) has ~149 KB available, which is generous for an embedded robotics controller.

### 4.3 Memory Monitoring

The `HeapMonitor` (safety system) runs every 1000 executor ticks (1 second). Thresholds:
- **Warning**: Free heap < 32 KB
- **Critical**: Free heap < 16 KB or largest free block < 8 KB
- **Action**: Shed non-critical nodes (telemetry, LED, audio) when critical.

---

## 5. Boot Sequence and Initialization Order

```
POWER ON
    |
[1] ESP-IDF hardware init (GPIO, UART, ADC, clock)
    |
[2] Arduino framework init (Serial.begin)
    |
[3] Check reset reason
    |-- ESP_RST_TASK_WDT  -> Load crash log from NVS, log recovery event
    |-- ESP_RST_BROWNOUT   -> Check voltage, delay if still low
    |-- ESP_RST_POWERON    -> Normal boot
    |
[4] Initialize HardwareWatchdog (TWDT at 3000ms, BOD at 3.0V)
    |
[5] Initialize ErrorLog (ring buffer, load previous log from NVS)
    |
[6] Initialize ParameterServer (load all persistent params from NVS)
    |
[7] Initialize UartBusManager (claim all UART pins, validate no conflicts)
    |
[8] Initialize DriverManager and register all drivers:
    |   Priority 10: UartBusManager (already done)
    |   Priority 20: SabertoothBusDriver (autobaud)
    |   Priority 30: SabertoothMotorDriver x3 (foot L, foot R, dome)
    |   Priority 40: MaestroServoDriver (body, 6ch)
    |   Priority 41: MaestroServoDriver (dome, 11ch)
    |   Priority 50: MP3TriggerDriver
    |   Priority 60: AnalogSensorDriver (dome pot)
    |   Priority 70: DmaUartDriver (OpenMV) [optional]
    |   Priority 80: PenumbraCommDriver [optional]
    |   Priority 90: GpioDriver (LEDs)
    |   -> DriverManager::initAll() in priority order
    |   -> All motors disabled, all servos at neutral, all outputs off
    |
[9] Initialize MessageBroker (TopicRegistry, MessagePools, PriorityDispatcher)
    |
[10] Initialize MotorSafetyMonitor (register all motors from DriverManager)
     |
[11] Initialize Executor (1 kHz loop frequency, 8 KB stack, Core 1)
     |
[12] Create and register all Nodes:
     |   Safety-critical (initialized and activated first):
     |     SafetyMonitorNode (1000 Hz, CRITICAL priority)
     |     MotorDriverNode x3 (100 Hz, HIGH priority)
     |     DomeMotorDriverNode (100 Hz, HIGH priority)
     |   Control nodes:
     |     DriveControlNode (50 Hz, HIGH priority)
     |     DomeControlNode (50 Hz, NORMAL priority)
     |     ServoDriverNode x2 (20 Hz, NORMAL priority)
     |   Non-critical:
     |     AudioDriverNode (10 Hz, LOW priority)
     |     LEDNode (10 Hz, LOW priority)
     |     DomeSensorNode (20 Hz, LOW priority)
     |     TelemetryNode (1 Hz, BACKGROUND priority)
     |
[13] Initialize ControllerRegistry and RoleManager
     |   Load allowlist from NVS
     |   Load role preferences from NVS
     |   Set assignment policy (MacBasedPolicy default)
     |
[14] Initialize Bluepad32
     |   BP32.setup(onConnect, onDisconnect, startScanning=true)
     |   -> System is now in SAFE_STOP mode (no controller connected)
     |   -> LED pattern: slow purple pulse (BT SEARCHING)
     |
[15] Start Executor main loop
     |   -> All safety watchdogs are active
     |   -> Motor safety timers are armed
     |   -> Waiting for controller connection
     |
[16] Controller connects
     |   -> Allowlist check, role assignment
     |   -> ControllerInputNode created and activated
     |   -> System transitions to FULL_OPERATION
     |   -> LED pattern: solid green
     |
[17] Normal operation
```

---

## 6. Topic Graph

### 6.1 Complete Publisher/Subscriber Map

```
TOPIC                          PRI   PUBLISHERS              SUBSCRIBERS
---------------------------------------------------------------------------
safety/estop                   255   SafetyMonitorNode       ALL NODES (broadcast)
safety/degradation             255   SafetyMonitorNode       Executor

controller/0/input             128   ControllerInputNode[0]  (debug/logging only)
controller/1/input             128   ControllerInputNode[1]  (debug/logging only)
controller/2/input             128   ControllerInputNode[2]  (debug/logging only)
controller/3/input             128   ControllerInputNode[3]  (debug/logging only)
controller/drive/input         128   ControllerInputNode[D]  DriveControlNode
controller/dome/input          128   ControllerInputNode[M]  DomeControlNode
controller/animation/input     128   ControllerInputNode[A]  AnimationNode
controller/camera/input        128   ControllerInputNode[C]  CameraNode
controller/role                128   RoleManager             ControllerInputNodes
controller/status              64    BatteryMonitorNode      TelemetryNode, LEDNode

drive/cmd                      192   DriveControlNode        MotorDriverNode (foot L+R)
dome/cmd                       192   DomeControlNode         DomeMotorDriverNode
servo/body/cmd                 128   AnimationNode           ServoDriverNode (body)
servo/dome/cmd                 128   AnimationNode, Camera   ServoDriverNode (dome)

sensor/dome_position           64    DomeSensorNode          DomeControlNode (future)
audio/cmd                      64    AnimationNode           AudioDriverNode
led/cmd                        64    LEDNode logic           GpioDriver

system/status                  64    SafetyMonitorNode       TelemetryNode
system/diagnostics             0     All (on request)        DebugShell
```

### 6.2 Topic Count Summary

| Category | Count | Topic IDs |
|----------|-------|-----------|
| Safety | 2 | safety/estop, safety/degradation |
| Controller (slot) | 4 | controller/{0,1,2,3}/input |
| Controller (role) | 4 | controller/{drive,dome,animation,camera}/input |
| Controller (meta) | 2 | controller/role, controller/status |
| Actuator commands | 4 | drive/cmd, dome/cmd, servo/body/cmd, servo/dome/cmd |
| Sensors | 1 | sensor/dome_position |
| Effects | 2 | audio/cmd, led/cmd |
| System | 2 | system/status, system/diagnostics |
| **Total** | **21** | Within MAX_TOPICS=32 budget |

---

## 7. Timing Budget

### 7.1 Multi-Rate Executor Design

Resolution of the 1 kHz vs 100 Hz conflict: the executor loop runs at **1 kHz** (1000 us period). Nodes declare their own frequency and are only invoked when their period elapses.

```
Tick 0    [SAFETY][MSGS][ SafetyNode | ControllerInput ][STATS][ sleep ]
Tick 1    [SAFETY][MSGS][ SafetyNode                   ][STATS][ sleep ]
Tick 2    [SAFETY][MSGS][ SafetyNode | ControllerInput ][STATS][ sleep ]
...
Tick 9    [SAFETY][MSGS][ SafetyNode | ControllerInput ][STATS][ sleep ]
Tick 10   [SAFETY][MSGS][ SafetyNode | CI | Drive | Dome | Motor x3 ][STATS][ sleep ]
Tick 20   [SAFETY][MSGS][ SafetyNode | CI | Drive | Dome | Servo x2 ][STATS][ sleep ]
Tick 100  [SAFETY][MSGS][ SafetyNode | CI | Drive | Audio | LED ][STATS][ sleep ]
```

### 7.2 Worst-Case Per-Tick Budget (1000 us)

| Phase | Budget | Worst Case | Notes |
|-------|--------|------------|-------|
| Phase 0: HW WDT feed | 5 us | 5 us | `esp_task_wdt_reset()` |
| Phase 1: Safety checks | 50 us | 40 us | Motor safety + BOD + connection monitor |
| Phase 2: Message dispatch | 200 us | 150 us | Priority dispatcher processes queued messages |
| Phase 3: Node execution | 600 us | varies | See node schedule below |
| Phase 4: Statistics | 50 us | 30 us | EMA update, periodic heap/stack check |
| Phase 5: Sleep | 95 us | 0 us | `vTaskDelayUntil` for remaining time |
| **Total** | **1000 us** | | |

### 7.3 Per-Node WCET and Schedule

| Node | Frequency | Period | WCET | Notes |
|------|-----------|--------|------|-------|
| SafetyMonitorNode | 1000 Hz | 1 ms | 30 us | Runs every tick. No I/O. |
| ControllerInputNode (x1-4) | 100 Hz | 10 ms | 50 us each | Read cached BT data, normalize, publish |
| DriveControlNode | 50 Hz | 20 ms | 40 us | Map input to motor commands, publish |
| DomeControlNode | 50 Hz | 20 ms | 40 us | Map input to dome command, publish |
| MotorDriverNode (foot L) | 50 Hz | 20 ms | 20 us* | Write to TX buffer only |
| MotorDriverNode (foot R) | 50 Hz | 20 ms | 20 us* | Write to TX buffer only |
| DomeMotorDriverNode | 50 Hz | 20 ms | 20 us* | Write to TX buffer only |
| ServoDriverNode (body) | 20 Hz | 50 ms | 50 us* | Batched Maestro command |
| ServoDriverNode (dome) | 20 Hz | 50 ms | 50 us* | Batched Maestro command |
| AnimationNode | 20 Hz | 50 ms | 80 us | Sequence state machine |
| AudioDriverNode | 10 Hz | 100 ms | 30 us | MP3Trigger polling |
| LEDNode | 10 Hz | 100 ms | 20 us | GPIO/PWM write |
| DomeSensorNode | 20 Hz | 50 ms | 30 us | ADC read + filter |
| BatteryMonitorNode | 0.2 Hz | 5000 ms | 20 us | Read cached battery levels |
| TelemetryNode | 1 Hz | 1000 ms | 50 us | Compile and publish SystemStatus |

*Motor/servo driver node WCET assumes non-blocking serial writes (buffered TX).

### 7.4 Worst-Case Tick Analysis

The heaviest tick occurs when all nodes fire simultaneously (every 100 ms = LCM of all periods):

```
SafetyMonitorNode:      30 us
ControllerInputNode x4: 200 us  (4 controllers)
DriveControlNode:       40 us
DomeControlNode:        40 us
MotorDriverNode x3:     60 us
ServoDriverNode x2:     100 us
AnimationNode:          80 us
AudioDriverNode:        30 us
LEDNode:                20 us
DomeSensorNode:         30 us
TelemetryNode:          50 us
BatteryMonitorNode:     20 us
---------------------------------
Total node execution:   700 us
+ Safety phase:         50 us
+ Message dispatch:     150 us
+ Statistics:           30 us
---------------------------------
Total worst case:       930 us  (70 us margin)
```

This is tight. If 4 controllers are rarely connected simultaneously, typical worst case is ~530 us (1 controller). The 930 us scenario is an edge case that may occasionally trigger a soft overrun. The safety system allows 3 consecutive soft overruns before taking action.

### 7.5 Runtime SoftwareSerial TX Behavior

Runtime SoftwareSerial TX is blocking and executes on the caller core. For actuator/audio command paths, the caller is normally the CPU1 executor. It is not a background TX ISR.

At 38400 baud, a Sabertooth/SyRen 8N1 byte takes about 0.26 ms and a four-byte Sabertooth packet takes about 1.04 ms. MP3 Trigger remains at 9600 baud. Powered testing should measure loop-time impact during multi-device command bursts.

If SoftwareSerial cannot be made non-blocking, use a dedicated low-priority FreeRTOS task on Core 1 with a 2 KB stack for serial TX. Nodes enqueue commands (lock-free ring buffer); the TX task sends them.

---

## 8. Configuration Management

### 8.1 Three-Tier Configuration

```
TIER 1: Compile-Time Defaults          [chopper_defaults.h]
    |   System limits (MAX_NODES, MAX_TOPICS, etc.)
    |   Pin assignments (GPIO numbers)
    |   Protocol constants (baud rates, addresses)
    |   Safety thresholds (never overridable at runtime)
    |
    v
TIER 2: NVS Persistent Storage         [ParameterServer + NvsConfig]
    |   Motor tuning (max speed, deadband, ramp rate)
    |   Servo calibration (min/max/neutral per channel)
    |   Controller preferences (MAC-to-role mapping)
    |   Audio volume
    |   Loaded at boot; persisted on explicit save
    |
    v
TIER 3: Runtime Parameters             [ParameterServer API]
        Same values as Tier 2 but changeable at runtime
        via debug shell or web UI.
        Written to NVS only on explicit save command
        or graceful shutdown.
```

### 8.2 Unified Parameter System

The `ParameterServer` (message system design) is the single public API for all runtime-configurable values. The `NvsConfig` (HAL design) is an internal implementation detail used by `ParameterServer::loadFromNVS()` and `saveToNVS()`.

HAL drivers declare their parameters through the ParameterServer at init time:

```cpp
// In SabertoothMotorDriver::init()
auto& ps = ParameterServer::getInstance();
ps.declare("drive.max_speed", 0.25f, 0.0f, 1.0f, /*persistent=*/true);
ps.declare("drive.ramp_rate", 80, 1, 255, /*persistent=*/true);
ps.declare("drive.deadband", 0.05f, 0.0f, 0.3f, /*persistent=*/true);
```

### 8.3 System Limits Header

All MAX_* constants in one file, shared by all subsystems:

```cpp
// chopper_limits.h
#pragma once
namespace chopper::limits {
    constexpr size_t MAX_NODES                   = 32;
    constexpr size_t MAX_TOPICS                  = 32;
    constexpr size_t MAX_SUBSCRIBERS_PER_TOPIC   = 8;
    constexpr size_t MAX_MOTORS                  = 16;
    constexpr size_t MAX_CONTROLLERS             = 4;
    constexpr size_t MAX_TIMERS                  = 16;
    constexpr size_t MAX_PARAMETERS              = 64;
    constexpr size_t MAX_SERVICES                = 8;
    constexpr size_t MAX_DRIVERS                 = 12;
    constexpr size_t MAX_TASKS                   = 8;
    constexpr size_t MAX_ERROR_LOG_ENTRIES        = 128;
}
```

---

## 9. Unified Emergency Stop Chain

### 9.1 Single E-Stop Path

Resolves the three-independent-e-stop problem identified in cross-cutting analysis:

```
TRIGGER (any of):
    - MotorSafetyMonitor timeout
    - ControllerConnectionMonitor all-lost
    - Node health: critical node crashed
    - BrownOutDetector critical voltage
    - Executor: manual e-stop command
    - HardwareWatchdog: TWDT about to fire
        |
        v
SafetyManager::emergencyStop(reason, source_id)
        |
        +--[1]--> MotorSafetyMonitor::disableAll()
        |             Stop all motors via direct function pointer callbacks.
        |             No message broker involved. No heap allocation.
        |             Uses const char* reason, not std::string.
        |
        +--[2]--> DriverManager::emergencyShutdown()
        |             Calls stop() on all IMotorDriver instances.
        |             Calls disableAll() on all IServoController instances.
        |             Does NOT call shutdown() (drivers remain initialized for recovery).
        |
        +--[3]--> Executor::notifyEmergencyStop()
        |             Sets e-stop flag.
        |             Calls node->emergencyStop() on all active nodes.
        |             Nodes zero their internal state.
        |
        +--[4]--> ErrorLog::log(SAFETY_ESTOP, source_id, ...)
        |
        +--[5]--> LED status -> rapid red flash
        |
        +--[6]--> Controller rumble feedback (if connected)
```

Key properties:
- Steps [1] and [2] execute in <100 us total (direct function calls, no allocation).
- The message broker is NOT used for the e-stop path. Safety cannot depend on the pub/sub system.
- `std::string` is never used in this path. All reason strings are `const char*` literals.

---

## 10. HAL Bridge Node Pattern

### 10.1 Connecting IDriver to the Node Graph

Each hardware driver is wrapped in a "bridge node" that participates in the node lifecycle, subscribes to command topics, and reports driver health.

```cpp
class MotorDriverNode : public PublishingNode {
public:
    MotorDriverNode(const char* name, IMotorDriver* driver, uint8_t motor_safety_id)
        : PublishingNode(name), driver_(driver), safety_id_(motor_safety_id) {}

    bool initialize() override {
        cmd_sub_ = createSubscription<MotorCommand>(
            topics::TOPIC_drive_cmd,
            &MotorDriverNode::onMotorCommand, this);
        return driver_->getStatus() == DriverStatus::kReady;
    }

    void process(uint64_t now) override {
        // Periodic: check driver health, update safety monitor
        if (driver_->getStatus() >= DriverStatus::kError) {
            ErrorLog::getInstance().log(ErrorLog::BUS_ERROR, safety_id_,
                                         driver_->getErrorState().code);
        }
    }

    void emergencyStop() override {
        driver_->stop();
        MotorSafetyMonitor::getInstance().feed(safety_id_);
    }

    double getUpdateFrequency() const override { return 50.0; }

private:
    void onMotorCommand(const MotorCommand& cmd) {
        driver_->set(cmd.value);
        MotorSafetyMonitor::getInstance().feed(safety_id_);
    }

    IMotorDriver* driver_;
    uint8_t safety_id_;
    SubscriptionHandle cmd_sub_;
};
```

### 10.2 Bridge Node Inventory

| Bridge Node | Subscribes To | Driver Interface | Safety Registration |
|-------------|--------------|-----------------|-------------------|
| MotorDriverNode (foot L) | drive/cmd | IMotorDriver | MotorSafety motor 0 |
| MotorDriverNode (foot R) | drive/cmd | IMotorDriver | MotorSafety motor 1 |
| DomeMotorDriverNode | dome/cmd | IMotorDriver | MotorSafety motor 2 |
| ServoDriverNode (body) | servo/body/cmd | IServoController | MotorSafety servo group |
| ServoDriverNode (dome) | servo/dome/cmd | IServoController | MotorSafety servo group |
| AudioDriverNode | audio/cmd | IAudioDriver | None (not safety-critical) |
| DomeSensorNode | (publishes) | ISensorDriver | None |

---

## 11. Coding Conventions (Unified)

### 11.1 Resolved from Cross-Cutting Analysis

| Convention | Decision | Rationale |
|-----------|----------|-----------|
| Enum style | `kPrefixCase` in `enum class` | C++ idiomatic; consistent with HAL and safety |
| Callback types | `void(*)(args..., void* ctx)` function pointers | No heap allocation from `std::function` captures |
| String identifiers | `const char*` (flash-stored) | No `std::string` anywhere in framework |
| Priority direction | Higher number = higher priority | Consistent with message system (255=CRITICAL) |
| Init order | Separate `init_order` field (lower = earlier) | Distinct from priority |
| Node update method | `process(uint64_t now_us)` | Timestamp passed in; drivers adapt |
| Error reporting | All subsystems log to `ErrorLog` | Single source of truth for diagnostics |
| Singleton pattern | `getInstance()` with option for test injection | `setInstance()` for unit tests only |

---

## 12. Migration Path from Current sketch.cpp

### 12.1 Current Architecture

```cpp
// sketch.cpp (current)
setup() {
    setupBluepad32();     // BP32.setup(onConnect, onDisconnect)
    setupSabertooth();    // SoftwareSerial init, autobaud, config
    setupMaestro();       // SoftwareSerial init, timeout config
    setupMp3Trigger();    // Serial init, volume
    setupRssMachine();    // RSS config, neck servo enable
    setupOpenMV();        // HW UART init
    setupLeds();          // GPIO pinMode
}

loop() {
    BP32.update();                    // Poll BT
    myControllers.processInputs();    // Monolithic: reads all input, drives all output
    analogWrite(PIN_LED_FRONT, brightness);
    vTaskDelay(pdMS_TO_TICKS(10));    // 100 Hz
}
```

### 12.2 Phase 1: Introduce HAL Interfaces (Non-Breaking)

Keep `sketch.cpp` loop structure. Wrap existing hardware objects behind `IDriver` interfaces.

**Changes**:
- Create `IMotorDriver`, `IServoController`, `IAudioDriver`, `ISensorDriver` interfaces.
- Create adapter classes: `SabertoothMotorDriverAdapter` wraps existing `SabertoothController`.
- Create `DriverManager`, register all adapted drivers.
- Replace `setup*()` functions with `DriverManager::initAll()`.
- `loop()` calls `DriverManager::updateAll()` instead of ad-hoc updates.

**sketch.cpp after Phase 1**:
```cpp
setup() {
    DriverManager::getInstance().initAll();
    setupBluepad32();
}

loop() {
    BP32.update();
    myControllers.processInputs();  // still monolithic
    DriverManager::getInstance().updateAll();
    vTaskDelay(pdMS_TO_TICKS(10));
}
```

**Testing**: Existing behavior unchanged. Can run with `CHOPPER_MOCK_HAL` for testing.

### 12.3 Phase 2: Introduce Message System and Core Nodes

Replace `Controllers::processInputs()` with node-based pub/sub.

**Changes**:
- Implement `MessageBroker` with `TopicRegistry`, `MessagePool`, `PriorityDispatcher`.
- Create `ControllerInputNode` (reads BP32, publishes `ControllerInput`).
- Create `DriveControlNode` (subscribes `ControllerInput`, publishes `MotorCommand`).
- Create `MotorDriverNode` (subscribes `MotorCommand`, calls `IMotorDriver::set()`).
- Create `Executor` with 1 kHz loop.
- Remove `Controllers` class.

**sketch.cpp after Phase 2**:
```cpp
setup() {
    DriverManager::getInstance().initAll();
    MessageBroker::getInstance();  // init
    setupNodes();                   // create and register all nodes
    setupBluepad32();               // connects ControllerRegistry
    Executor::getInstance().start(); // starts 1 kHz FreeRTOS task
}

loop() {
    // Executor runs in its own task; loop() can be empty or do non-critical work
    vTaskDelay(pdMS_TO_TICKS(1000));
}
```

**Testing**: Each node testable independently with mock drivers and mock message broker.

### 12.4 Phase 3: Safety System Integration

**Changes**:
- Implement `HardwareWatchdog`, `MotorSafetyMonitor`, `NodeHealthMonitor`.
- Implement `SafetyMonitorNode` and degradation mode state machine.
- Implement `TimingEnforcer` in executor.
- Add `ErrorLog` ring buffer.
- Implement unified e-stop chain via `SafetyManager`.

### 12.5 Phase 4: Multi-Controller Bluetooth

**Changes**:
- Implement `ControllerRegistry`, `RoleManager`, `ControllerAllowlist`.
- Implement `DisconnectHandler` with role-specific fallback.
- Implement `BatteryMonitorNode`.
- Migrate MAC-to-role mapping from `SettingsBluetooth.h` to NVS.

### 12.6 Phase 5: Advanced Features

**Changes**:
- `ParameterServer` with NVS persistence and debug shell.
- Web dashboard (optional WiFi, HTTP server on Core 0).
- OTA update support.
- Animation sequencer node.

### 12.7 Phase Dependencies

```
Phase 1 (HAL)
    |
    v
Phase 2 (Message System + Core Nodes)
    |
    +------+
    |      |
    v      v
Phase 3  Phase 4
(Safety) (Bluetooth)
    |      |
    +------+
    |
    v
Phase 5 (Advanced Features)
```

Phases 3 and 4 can proceed in parallel after Phase 2 is complete.

---

## 13. Testing Strategy

### 13.1 Unit Tests (Host PC, no hardware)

| Test Suite | What It Tests | Mock Dependencies |
|-----------|--------------|-------------------|
| `test_message_pool` | Pool acquire/release, exhaustion, generation counter | None |
| `test_spsc_ring` | Push/pop, wrap-around, full/empty detection | None |
| `test_topic_registry` | Register, lookup, hash collision handling | None |
| `test_priority_dispatcher` | Priority ordering, budget enforcement | MockTopicRegistry |
| `test_parameter_server` | Declare, get, set, range validation, callbacks | MockNVS |
| `test_motor_safety` | Timeout detection, feed reset, disable all | MockClock |
| `test_node_health` | Overrun counting, action thresholds | MockClock |
| `test_timing_enforcer` | WCET measurement, EMA calculation | MockClock |
| `test_error_log` | Ring buffer wrap, concurrent writes, severity counting | None |
| `test_role_manager` | Assignment policies, swap, reconnect hold | MockControllerSlot |
| `test_disconnect_handler` | Per-role fallback behavior | None |
| `test_controller_registry` | Connect/disconnect, allowlist, slot management | MockBP32 |
| `test_slew_rate_limiter` | Ramp up/down, zero crossing | None |
| `test_drive_control_node` | Joystick-to-motor mapping, deadband | MockPublisher |
| `test_motor_driver_node` | Command-to-driver call mapping | MockMotorDriver |

**Build system**: CMake with `CHOPPER_MOCK_HAL=ON` and a host toolchain (clang/gcc). No ESP-IDF needed for unit tests.

### 13.2 Integration Tests (ESP32 target, no peripherals)

| Test | What It Validates |
|------|------------------|
| Full boot sequence | All components initialize without errors on real ESP32 |
| Executor timing | Measure actual loop jitter with `esp_timer_get_time()`; verify <5% variance |
| Message round-trip | Publish on one node, verify delivery on subscriber with latency measurement |
| Parameter NVS persistence | Write param, reboot, verify value restored |
| Error log persistence | Log entries, reboot, verify entries recovered from NVS |
| Heap stability | Run executor for 10 minutes with mock controllers; verify no heap growth |
| Stack high-water marks | Run all nodes; verify all stacks have >20% margin |

Uses `CHOPPER_MOCK_HAL=ON` on real ESP32 hardware. All drivers are mocked, but FreeRTOS, timers, NVS, and memory management are real.

### 13.3 Hardware-in-the-Loop Tests (ESP32 + peripherals)

| Test | Hardware Needed | What It Validates |
|------|----------------|------------------|
| Motor command end-to-end | Sabertooth + motor | Joystick input -> motor spin with correct direction/speed |
| Servo sweep | Maestro + servo | Servo moves to commanded position and back |
| Controller disconnect safety | BT controller | Disconnect controller -> motors stop within 200 ms |
| Audio playback | MP3 Trigger + speaker | Sound triggers play correctly |
| Dome sensor feedback | Potentiometer on ADC | ADC reads correctly, filtering works |
| Multi-controller | 2+ BT controllers | Both connect, roles assigned, input routed correctly |
| Brown-out response | Variable power supply | Reduce voltage -> degradation mode activates |
| Watchdog recovery | (trigger via long delay) | TWDT fires, system reboots, crash log recovered |
| Long-duration stability | All hardware | Run for 1+ hours continuous operation; verify no memory leak or timing degradation |

### 13.4 Simulation Mode

For development without physical hardware, a `SimulationNode` can inject synthetic controller input:

```cpp
class SimControllerNode : public PublishingNode {
    // Generates predefined input patterns:
    // - Sine wave on drive axes (test smooth motion)
    // - Button press sequences (test animations)
    // - Disconnect/reconnect cycles (test safety)
    // Controlled via debug shell commands
};
```

---

## 14. Development Workflow

### 14.1 Build Targets

| Target | Toolchain | HAL | Purpose |
|--------|-----------|-----|---------|
| `host_test` | clang/gcc | Mock | Unit tests on developer machine |
| `esp32_test` | xtensa-gcc (ESP-IDF) | Mock | Integration tests on ESP32 |
| `esp32_release` | xtensa-gcc (ESP-IDF) | Real | Production firmware |

### 14.2 Build Commands

```bash
# Unit tests (host PC)
cmake -B build/host -DCHOPPER_MOCK_HAL=ON -DCHOPPER_HOST_TEST=ON
cmake --build build/host
./build/host/chopper_tests

# ESP32 integration tests (mock HAL)
idf.py -DCHOPPER_MOCK_HAL=ON build flash monitor

# ESP32 production firmware
idf.py build flash monitor
```

### 14.3 Debug Workflow

1. **Serial console**: 115200 baud, USB. Access debug shell for topic/param/error introspection.
2. **JTAG debugging**: ESP-IDF supports OpenOCD + GDB for breakpoints and memory inspection.
3. **ESP-IDF Monitor**: `idf.py monitor` for crash backtraces with symbol resolution.
4. **Heap tracing**: `CONFIG_HEAP_TRACING_STANDALONE` to detect leaks during development.
5. **System view**: `esp_sysview` for real-time FreeRTOS task timing visualization.

### 14.4 Flash Layout

```
0x0000 - 0x8FFF    Bootloader (36 KB)
0x9000 - 0x9FFF    Partition table (4 KB)
0xA000 - 0xDFFF    NVS (16 KB) -- parameters, BT allowlist, error log
0xE000 - 0xEFFF    OTA data (4 KB) -- for future OTA support
0x10000 - 0x1FFFFF App partition (1.9 MB)
0x200000 - 0x3FFFFF OTA partition (2 MB) -- future: second app slot for OTA
```

---

## 15. Future Extensibility

### 15.1 WiFi Telemetry and Web Dashboard

**When**: Phase 5, after core system is stable.

**Architecture**: WiFi station mode, HTTP server on Core 0.

```
ESP32 Core 0                 Browser
+-----------+                +----------+
| HTTP      |  <-- WiFi -->  | Web UI   |
| Server    |                | (React)  |
+-----------+                +----------+
      |
      v
+------------------+
| JSON API         |
| GET /api/topics  |
| GET /api/params  |
| POST /api/params |
| GET /api/status  |
| GET /api/errors  |
| WS /api/stream   |  <-- WebSocket for live topic echo
+------------------+
      |
      v
+------------------+
| ParameterServer  |  (read/write via atomic generation counter)
| TopicRegistry    |  (read-only statistics)
| ErrorLog         |  (read-only)
+------------------+
```

**Memory impact**: WiFi adds ~45 KB SRAM. Web server task stack: 8 KB. Total: ~53 KB. This reduces available application memory from 149 KB to ~96 KB. WiFi should be optional and activatable via a parameter or physical switch.

### 15.2 OTA Updates

**When**: Phase 5+.

**Architecture**: Two app partitions (A/B). OTA downloads new firmware to inactive partition. On reboot, bootloader switches. Rollback if new firmware fails health check within 30 seconds.

**Safety**: OTA update only allowed when system is in SAFE_STOP mode (all motors stopped). The safety system cannot be updated via OTA (bootloader partition is read-only).

### 15.3 ROS2 Micro Bridge

**When**: Future, if desktop integration is desired.

**Architecture**: micro-ROS agent on a companion PC. The ESP32 runs a micro-ROS transport over UART (HW UART2, replacing or time-sharing with OpenMV). Topics are bridged 1:1 between the chopper topic system and ROS2 topics.

This enables:
- RViz visualization of robot state
- ROS2 Navigation stack for autonomous waypoint following
- Rosbag recording of all topics for replay/analysis

**Prerequisite**: The chopper topic system already uses ROS2-compatible concepts (topics, QoS, pub/sub). The bridge is a thin translation layer.

### 15.4 Camera / Vision Integration

**When**: After OpenMV UART driver is stable.

**Architecture**: OpenMV runs vision algorithms (face tracking, object detection). Results are sent via UART to the ESP32. A `VisionNode` deserializes the UART data and publishes `VisionResult` messages. Other nodes (CameraNode, AnimationNode) subscribe to trigger head tracking or reactive animations.

---

## 16. Open Decisions for Implementation

| # | Decision | Options | Recommendation | Owner |
|---|----------|---------|---------------|-------|
| 1 | SoftwareSerial replacement | (a) Keep SoftwareSerial + stagger, (b) Non-blocking TX task, (c) Use dedicated hardware UARTs where pins allow | Runtime keeps actuator/audio SoftwareSerial at 9600 for compatibility and timing margin; measure loop margin before further changes | HAL implementer |
| 2 | WiFi: always-on vs on-demand | (a) Always-on, (b) Enable via parameter, (c) Physical switch | (b) Parameter-controlled, default off | Systems implementer |
| 3 | Physical E-STOP button | (a) Software only, (b) GPIO-wired kill switch on motor enable pins | (b) Recommended for safety but not required for indoor use | Hardware decision |
| 4 | Exception handling | (a) Keep try/catch, (b) Compile with `-fno-exceptions` | (b) Disable exceptions, use error codes/ErrorLog | Core implementer |
| 5 | RTTI | (a) Keep, (b) Compile with `-fno-rtti` + compile-time type IDs | (b) Disable RTTI after migrating to `getTypeId()` | Core implementer |
| 6 | Executor stack size | (a) 4 KB current, (b) 8 KB recommended | (b) 8 KB, measure with StackMonitor, reduce if warranted | Core implementer |
| 7 | PenumbraComm buffer sizes | (a) Keep 25x256, (b) Reduce to 8x64 | (b) Reduce; saves ~6 KB SRAM | HAL implementer |
| 8 | Controller input frequency | (a) 50 Hz (current), (b) 100 Hz (BT design) | (b) 100 Hz for responsiveness; deadband filter limits actual publishes | BT implementer |

---

## 17. Summary

This architecture integrates five subsystem designs into a coherent system:

- **Executor** at 1 kHz with multi-rate node scheduling resolves the frequency mismatch.
- **HAL bridge nodes** connect IDriver instances to the pub/sub graph.
- **Unified e-stop chain** ensures a single, reliable safety path that does not depend on the message broker.
- **ParameterServer** is the single API for all configuration, backed by NVS internally.
- **Memory budget** of ~131 KB leaves ~149 KB for application logic (BT-only) or ~96 KB (with WiFi).
- **Migration** proceeds in 5 phases, each delivering testable increments, with Phases 3 and 4 parallelizable.

The system is designed to run safely on a single-core ESP32 with BT Classic, with clear extension points for dual-core, WiFi, web UI, OTA, and ROS2 integration.
