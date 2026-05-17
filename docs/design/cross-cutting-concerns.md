# Cross-Cutting Concerns Analysis

**Date**: 2026-02-27
**Author**: core-architect
**Inputs reviewed**:
- `docs/design/core-framework-audit.md` (core-architect)
- `docs/design/message-system-design.md` (systems-designer)
- `docs/design/safety-realtime-design.md` (systems-designer)
- `docs/design/hal-design.md` (hal-designer)

---

## 1. Conflicting Assumptions

### 1.1 Executor Loop Frequency: 1 kHz vs 100 Hz

| Document | Assumed Loop Frequency |
|----------|----------------------|
| Core audit (Executor.h) | **1 kHz** (1000 us per loop; `loop_frequency_hz = 1000`) |
| Message system design (Sec 8.1) | **1 kHz** (1000 us budget, matches core) |
| Safety/RT design (Sec 8.1) | **1 kHz** (1000 us total budget split across phases) |
| HAL design (Sec 8.2) | **100 Hz** (10 ms target, matching current `vTaskDelay(pdMS_TO_TICKS(10))`) |

**Conflict**: The HAL design assumes a 10 ms main loop period and budgets driver updates at 4 ms (Section 8.2). The core/messaging/safety designs all assume a 1 ms loop. At 1 kHz, the HAL driver update budget of 4 ms would exceed the entire loop period by 3x.

**Resolution needed**: The system must operate at one consistent frequency. Two viable approaches:
1. **Single-rate at 1 kHz**: All nodes including HAL driver nodes must complete their work within the 1 ms budget. HAL drivers must stagger their updates (e.g., Sabertooth on tick N, Maestro body on tick N+1, etc.) rather than updating all drivers every tick.
2. **Multi-rate with a fast safety loop**: Run the executor at 1 kHz but HAL driver updates at 100 Hz (every 10th tick). The safety/messaging system runs every tick; HAL `updateAll()` runs every 10th tick.

Recommendation: Option 2 (multi-rate) aligns with the node frequency concept already in the Executor. HAL driver nodes declare `getUpdateFrequency() = 100` (100 Hz), while safety nodes declare 1000 Hz.

### 1.2 Sabertooth Timing Budget

The HAL design (Section 11, Open Question #5) identified that transmitting 3 Sabertooth motor commands at the legacy 9600 baud took ~12.6 ms. Runtime now uses 19200 baud for the shared Sabertooth/SyRen bus as a compatibility midpoint, reducing the same burst to ~6.3 ms while keeping the packet protocol unchanged.

**This is a hard physical constraint**: At runtime 19200 baud, one 4-byte packet takes ~2.1 ms to transmit. This improves executor timing margin over the legacy 9600 baud path, but multi-motor command bursts are still blocking unless unchanged commands are suppressed, updates are staggered, or serial TX is moved out of the executor path.

**Impact on timing budget**: If motors update at 50 Hz (every 20 ms), each changed motor command costs about 2.1 ms at 19200 baud. Two drive packets cost about 4.2 ms; drive plus dome can cost about 6.3 ms. This must be explicitly modeled in the executor's timing analysis.

The safety/RT design's rate-monotonic analysis (Section 8.4) gives the DriveNode a WCET of 80 us at 50 Hz. But this budget does not include the blocking serial TX time of ~2.1 ms per motor command at 19200 baud. If the serial TX is blocking (which SoftwareSerial is), the actual WCET is milliseconds, not 80 us.

**Resolution needed**: The DriveNode WCET must account for serial TX time, OR serial TX must be made non-blocking (buffer + ISR/DMA), OR the timing analysis must acknowledge that DriveNode is a special case that spans multiple loop iterations.

### 1.3 Executor Stack Size: 4 KB vs 8 KB

| Document | Executor Stack Size |
|----------|-------------------|
| Core audit (Executor.cpp:149) | **4096 bytes** |
| Safety/RT design (Sec 9.3) | **8192 bytes** |

The safety/RT design recommends 8 KB "runs all nodes, needs headroom." The current implementation allocates 4 KB. Both estimates are plausible depending on node complexity, but they need to converge on one value.

**Recommendation**: Start at 8 KB as the safety design suggests. The StackMonitor can detect if this is oversized and it can be reduced once high-water marks are measured on target.

### 1.4 Maximum Number of Topics vs Subscriptions

| Resource | Message Design | Safety/RT Design |
|----------|---------------|-----------------|
| MAX_TOPICS | 32 | Not specified |
| MAX_SUBSCRIBERS_PER_TOPIC | 8 | Not specified |
| MAX_NODES | 32 (core Executor) | 32 (NodeHealthMonitor) |
| MAX_MOTORS | Not specified | 16 |
| MAX_CONTROLLERS | Not specified | 4 |
| MAX_TIMERS | 16 | Not specified |
| MAX_PARAMETERS | 64 | Not specified |
| MAX_SERVICES | 8 | Not specified |
| MAX_DRIVERS | Not specified | Not specified (HAL: 12) |

These are consistent where they overlap, but the complete set of MAX_* constants needs to be centralized in one header file so all subsystems share the same limits.

### 1.5 ParameterServer vs NVS Configuration

The message system design defines a `ParameterServer` (Section 10) with `MAX_PARAMETERS = 64` parameters, NVS persistence, and change callbacks. The HAL design defines a separate `NvsConfig` loader (Section 5.2) with namespace-based NVS access.

**Conflict**: Two independent NVS access patterns for the same purpose. The ParameterServer declares parameters like `drive.max_speed` while the HAL's NVS schema stores `drive_maxspd` in the `hal_motor` namespace. If both systems persist to NVS, they will use different keys for the same logical parameter.

**Resolution needed**: Unify into one parameter system. Either:
- The ParameterServer is the single interface for all runtime parameters, and HAL drivers declare their parameters through it.
- Or the NvsConfig is a low-level loader used only at init, and the ParameterServer wraps it for runtime access.

Recommendation: The ParameterServer is the public API. It delegates persistence to NvsConfig internally. HAL drivers declare parameters through the ParameterServer at init time. The NVS namespace is the ParameterServer's implementation detail.

### 1.6 Priority Numbering Direction

| Document | Priority Convention |
|----------|-------------------|
| Message system design (Sec 4.3) | 0 = lowest, 255 = highest (CRITICAL = 255) |
| HAL design (DriverManager, Sec 4.2) | "Lower = higher priority" (10 = highest, 90 = lowest) |

**Conflict**: Opposite directions for the same concept. The message system uses ascending priority (255 = most important), while the HAL initialization order uses descending priority (10 = first/most important).

**Resolution needed**: Adopt one convention project-wide. Recommendation: Use the message system's convention (higher number = higher priority) for all priority concepts. The HAL's "initialization order" is a separate concept (sequence, not priority) and should be renamed to `init_order` or `init_phase` to avoid confusion.

---

## 2. Integration Gaps

### 2.1 HAL Drivers Are Not Nodes

The HAL design presents `DriverManager::updateAll()` as a flat loop called by the executor. But the core framework's primary abstraction is Nodes. HAL drivers (IDriver) do not participate in the node lifecycle (INACTIVE/ACTIVE/PAUSED/ERROR/SHUTDOWN), do not publish or subscribe to messages, and are not managed by the node health monitor.

**Gap**: There is no specification for how HAL drivers connect to the node graph. The example nodes in the core framework (`MotorControlNode`, `SensorNode`) hold raw pointers to drive/sensor classes. But the HAL design introduces `IMotorDriver*`, `IServoController*`, etc.

**Missing specification**: A "HAL Node" pattern that bridges `IDriver` to `Node`:

```
class MotorDriverNode : public PublishingNode {
    // Subscribes to MotorCommand topic
    // Owns an IMotorDriver* (injected via DriverManager)
    // Calls driver->set() in its callback
    // Reports driver health to NodeHealthMonitor
    // Feeds MotorSafetyMonitor on every command
};
```

This bridge pattern needs to be defined in the integration document. Without it, the safety system (which monitors nodes) and the HAL system (which monitors drivers) operate as parallel hierarchies with no interlock.

### 2.2 MotorSafety Dual Ownership

The safety design introduces `MotorSafetyMonitor` (Section 5.1) with its own `MAX_MOTORS = 16` motor registry and per-motor watchdog. The HAL design has `DriverManager` with `MAX_DRIVERS = 12` driver registry and per-driver timing enforcement.

Both systems monitor the same physical motors through different abstractions:
- `MotorSafetyMonitor` tracks "when was this motor last commanded?" (feed/timeout)
- `DriverManager` tracks "is this driver's update() running on time?"

**Gap**: Who is responsible for stopping a motor? If `MotorSafetyMonitor` detects a timeout, it calls `StopCallback`. But the motor is also managed by `DriverManager`. If the DriverManager thinks the driver is in `kError` state, does it also stop the motor? What if they disagree?

**Resolution needed**: Define clear ownership. Recommendation: `MotorSafetyMonitor` is the authoritative safety interlock. `DriverManager` handles lifecycle (init/update/shutdown). When MotorSafetyMonitor fires a timeout, it calls the motor driver's `stop()` method directly via the registered callback. DriverManager is informed of the stop via a state change but does not independently make safety decisions.

### 2.3 Emergency Stop Path: Three Distinct Implementations

The three documents define three different emergency stop mechanisms:

1. **Core Executor** (`Executor::emergencyStop(const std::string& reason)`): Iterates all nodes and calls `node->emergencyStop()`.
2. **Safety design** (`MotorSafetyMonitor::disableAll()`): Iterates all registered motors and calls their stop callbacks.
3. **HAL design** (`DriverManager::shutdownAll()`): Iterates all drivers and calls `shutdown()`.

**Gap**: These three mechanisms are not coordinated. If the Executor triggers an emergency stop, it calls nodes' `emergencyStop()` methods but does not call `MotorSafetyMonitor::disableAll()` or `DriverManager::shutdownAll()`. A node's `emergencyStop()` implementation might set motor speed to zero via a message, but the message delivery depends on the broker still running.

**Resolution needed**: A single emergency stop chain:
```
Trigger (any source)
    -> SafetyManager::emergencyStop(reason)
        -> MotorSafetyMonitor::disableAll()     // stops all motors immediately
        -> DriverManager::shutdownAll()          // disables all hardware
        -> Executor::emergencyStop()             // notifies all nodes
        -> ErrorLog::log(SAFETY_ESTOP, ...)     // records the event
        -> LED status -> rapid red flash
```

The `emergencyStop()` path must NOT depend on the message broker (synchronous delivery might fail if the broker is in a bad state). It must be a direct function call chain.

### 2.4 Error Logging: Where Do HAL Errors Go?

The safety design defines `ErrorLog` (Section 10.1) as a ring-buffer log for safety events. The HAL design defines `ErrorInfo` per-driver (Section 2.2) as a single error state per driver.

**Gap**: HAL driver errors are not connected to the ErrorLog. If a Sabertooth driver detects a bus error, it sets its own `ErrorInfo`, but the safety system has no way to know about it unless someone polls `DriverManager::checkHealth()`.

**Resolution needed**: HAL drivers should log to the ErrorLog when transitioning to `kDegraded` or `kError` state. The DriverManager's `checkHealth()` method should write to ErrorLog, not just flip driver status flags.

### 2.5 Message System Cleanup vs HAL Shutdown Order

The message system design (Section 13) describes `processPending(budget_us)` as the executor's message dispatch call. The HAL design (Section 4.4) places `DriverManager::updateAll()` before node processing in the executor tick.

**Gap**: During shutdown, the order matters:
- If nodes are deactivated first, they may try to publish final messages (e.g., "motor stop") that the broker cannot deliver because subscriptions are being torn down.
- If drivers are shut down first, node `emergencyStop()` callbacks that call `driver->stop()` will fail because the driver is already disabled.

**Resolution needed**: Define a shutdown sequence:
1. All nodes publish emergency stop messages (best-effort, synchronous delivery)
2. All motor safety watchdogs fire (stops all motors via direct callback)
3. Nodes are deactivated in reverse priority order
4. Drivers are shut down in reverse initialization order
5. MessageBroker is shut down last (allows any final diagnostic messages)

### 2.6 Timer Ownership: Message System vs Safety System

The message system design introduces `TimerManager` (Section 8) as part of the broker, with 16 timer slots. The safety system's revised Executor (Section 12.1) adds `timer_manager_.processDueTimers()` as a separate phase.

**Gap**: It's not clear if the TimerManager is owned by the MessageBroker or the Executor. The message system places it inside the broker (Section 13.1: `TimerManager timers_` as a member of `MessageBroker`). The safety design calls it as a separate phase in the executor loop.

**Resolution needed**: The TimerManager should be owned by the Executor, not the MessageBroker. The broker's job is message routing; timers are a scheduling concern. Nodes register timers through the executor (or through a convenience method in PublishingNode that delegates to the executor).

---

## 3. Combined Memory Budget Feasibility

### 3.1 Framework SRAM Budget (All Subsystems)

| Subsystem | Estimated SRAM | Source |
|-----------|---------------|--------|
| Core framework (Executor, Nodes, Broker) | ~10 KB | Core audit Section 8 |
| Message system (TopicRegistry, Pools, Queues, Stats, Timers, ParameterServer, Services) | ~13.8 KB | Message design Section 14.1 |
| Safety system (MotorSafety, ConnectionMonitor, HealthMonitor, TimingEnforcer, ErrorLog, StackMonitor, HeapMonitor) | ~6.2 KB | Safety design Section 14 |
| HAL drivers (all drivers + DriverManager + UART manager) | ~13.9 KB | HAL design Section 8.1 |
| **Subtotal: Framework** | **~43.9 KB** | |

### 3.2 System SRAM Budget (Non-Framework)

| Component | Estimated SRAM | Notes |
|-----------|---------------|-------|
| FreeRTOS kernel | ~10-15 KB | Task control blocks, queues, timers |
| Executor task stack | 8 KB | Safety design recommendation |
| BT stack (Bluepad32 + btstack) | ~50-70 KB | ESP-IDF BT classic + BLE |
| WiFi stack (if enabled) | ~40-60 KB | Can be disabled if not needed |
| ESP-IDF system overhead | ~15-20 KB | Heap metadata, IPC, logging |
| SoftwareSerial instances (4x) | ~1 KB | Bit-bang buffers |
| ESP-IDF UART driver buffers (HW UART2) | ~1.5 KB | DMA ring buffers |
| Arduino runtime (if used) | ~5-10 KB | Arduino HAL wrappers |
| **Subtotal: System** | **~130-185 KB** | |

### 3.3 Total Budget

| Category | Minimum | Maximum |
|----------|---------|---------|
| Framework | 44 KB | 44 KB |
| System | 130 KB | 185 KB |
| **Total** | **174 KB** | **229 KB** |
| **Available for application logic** | **91 KB** | **146 KB** |

ESP32-WROOM-32D total SRAM: 320 KB (of which ~160 KB is typically available after IRAM/DRAM split and BT/WiFi reservation).

**Risk assessment**: If WiFi is enabled alongside BT Classic (for web UI), the system SRAM usage peaks at ~229 KB, leaving only ~91 KB. This is tight but feasible if:
- WiFi is only enabled on demand (not always-on)
- The web server task stack is allocated from PSRAM (if available)
- Message pool sizes are tuned conservatively

If WiFi is disabled (BT-only operation), the budget is comfortable with ~146 KB free.

### 3.4 Overlap and Double-Counting

Several components are counted in multiple budgets:

1. **Core framework (10 KB) vs Message system (13.8 KB)**: The message system design *replaces* the existing core MessageBroker, Publishers, and Subscriptions. The 10 KB core estimate includes the current MessageBroker overhead (~2-4 KB). If the new message system replaces it, the combined total is approximately `10 - 4 + 13.8 = ~19.8 KB`, not `10 + 13.8 = 23.8 KB`.

2. **ParameterServer (in message system, ~3.6 KB) vs NVS Config (in HAL)**: If unified into one system (see Section 1.5), count only once: ~3.6 KB.

3. **TimerManager (in message system, ~0.5 KB)**: Counted once, regardless of whether it lives in the broker or executor.

**Revised framework total after deduplication**: ~38 KB (saving ~6 KB from double-counting).

### 3.5 PenumbraCommDriver: Largest Single Consumer

The HAL design notes that `PenumbraCommDriver` consumes ~7.5 KB due to dual RingBuffers with 25-slot x 260-byte messages. This is more than the entire safety subsystem.

**Recommendation**: Reduce `BUFFER_SIZE` from 25 to 8 and `BUFFER_DATA_MAX_SIZE` from 256 to 64. This would reduce the PenumbraCommDriver footprint from ~7.5 KB to ~1.5 KB, saving ~6 KB of SRAM. The current serial protocol does not require 256-byte payloads for LED commands or coordinate data.

---

## 4. Combined Timing Budget Feasibility

### 4.1 1 kHz Main Loop Budget

The safety design's timing budget (Section 8.1) allocates the 1000 us loop as:

| Phase | Budget | What |
|-------|--------|------|
| Safety checks | 50 us | HW WDT feed, motor safety, BOD |
| Message dispatch | 200 us | Priority-based delivery |
| Timer callbacks | (not budgeted) | TimerManager processing |
| Node execution | 600 us | All active node `process()` calls |
| Statistics | 50 us | Stats, periodic stack/heap check |
| Reserve (jitter) | 100 us | Interrupt overhead, context switches |

### 4.2 Per-Node WCET vs Serial TX

The safety design's per-node WCETs (Section 8.2) assume non-blocking operations:

| Node | WCET Budget | Actual (with serial TX) |
|------|------------|------------------------|
| SafetyMonitorNode | 30 us | 30 us (no I/O) |
| ControllerInputNode | 50 us | 50 us (reads cached BT data) |
| DriveNode | 80 us | **2080-4160 us** (1-2 Sabertooth packets at 19200 baud) |
| DomeNode | 60 us | **2080 us** (1 Sabertooth packet at 19200 baud) |
| ServoNode | 40 us | **1560-7000 us** (Maestro speed or 11-channel batched command at 38400 baud) |
| AudioNode | 30 us | **260-520 us** (1-2 byte MP3 Trigger serial commands at 38400 baud) |

**Critical finding**: DriveNode, DomeNode, and servo-command paths still exceed their original WCET budgets, even after moving the motor bus above legacy 9600 baud. The rate-monotonic analysis in the safety design (Section 8.4) shows 3.9% CPU utilization, but this is based on incorrect microsecond-only WCET assumptions that omit blocking serial TX.

**Corrected utilization**:
```
Safety:     30/1000   = 0.030
Controller: 50/20000  = 0.0025
Drive:      4200/20000 = 0.210  (was 0.004)
Dome:       4200/50000 = 0.084  (was 0.0012)
Servo:      500/50000  = 0.010  (was 0.0008)
Audio:      200/100000 = 0.002  (was 0.0003)
LED:        40/100000  = 0.0004
Telemetry:  50/1000000 = 0.00005
---------------------------------
Total U = 0.337 (33.7% CPU utilization)
```

33.7% is still within the schedulability bound (72.4% for 8 tasks), but the Drive and Dome nodes individually exceed the 1000 us per-loop budget. They cannot run within a single 1 kHz tick.

**Resolution options**:
1. **Non-blocking serial TX**: Buffer Sabertooth packets and transmit via ISR or DMA. The node's `process()` only writes to a buffer (~5 us), and transmission happens in the background. This is the cleanest solution but requires replacing SoftwareSerial with a proper buffered UART.
2. **Stagger across ticks**: Drive and dome nodes write one motor command per tick. At 1 kHz, a 3-motor update takes 3 ticks (3 ms), which is fine for 50 Hz control loops.
3. **Dedicated UART task**: Run serial TX on a separate low-priority FreeRTOS task. The node enqueues commands; the UART task transmits them. Adds a task stack (~2 KB) but completely decouples node timing from UART baud rate.

Recommendation: Option 2 (stagger) for the initial implementation, with a migration path to Option 1 (non-blocking) when SoftwareSerial is replaced.

### 4.3 Timer Callback Budget Not Allocated

The safety design's revised executor (Section 12.1) adds `timer_manager_.processDueTimers()` as Phase 3, between message dispatch and node execution. But the 1000 us budget does not allocate time for timer callbacks.

If timers fire node-like work (e.g., periodic sensor reads), their WCET must come from either the message dispatch budget (200 us) or the node execution budget (600 us).

**Recommendation**: Treat timer callbacks as part of the node execution phase. Fire timers during Phase 4 (node execution), interleaved with node processing based on their priority. This avoids creating a separate budget category.

---

## 5. Naming and Pattern Inconsistencies

### 5.1 Naming Conventions

| Concept | Core/Message | Safety | HAL |
|---------|-------------|--------|-----|
| Lifecycle states | `Node::State::ACTIVE` | `HealthStatus::HEALTHY` | `DriverStatus::kReady` |
| Emergency stop | `emergencyStop()` | `disableAll()` | `shutdownAll()` |
| Singleton access | `getInstance()` | `getInstance()` | singleton (DriverManager) |
| Status enum style | PascalCase (`ACTIVE`) | PascalCase (`HEALTHY`) | kPrefix (`kReady`) |
| Callback types | `std::function<void(...)>` | `void(*)(...)` (function pointer) | `void(*)(...)` |
| String parameter | `const std::string&` | `const char*` | `const char*` |

**Issues**:
1. **Enum naming style**: Core uses `SCREAMING_CASE`, HAL uses `kPrefixCase`. Pick one. Recommendation: `kPrefixCase` is more C++-idiomatic for enumerators in `enum class`.
2. **Callback types**: Core/message system uses `std::function` (heap-allocating on capture). Safety and HAL use raw function pointers with `void* context` (no heap). The safety/HAL approach is correct for embedded. The core should migrate to function pointers.
3. **String parameters**: Core uses `std::string` references, while safety and HAL use `const char*`. Per the core audit, `std::string` should be eliminated from hot paths. The entire codebase should standardize on `const char*` for names and identifiers.

### 5.2 Pattern Inconsistencies

1. **Singleton vs Injection**: `MessageBroker` and `ParameterServer` use singletons. `DriverManager` is a singleton. The core audit recommends making the broker injectable. Decision needed: singletons are acceptable for an embedded single-process system, but at minimum provide a way to reset/reinitialize them for testing.

2. **Health monitoring duplication**: `NodeHealthMonitor` (safety) monitors node execution health. `DriverManager::checkHealth()` (HAL) monitors driver health. `MotorSafetyMonitor` (safety) monitors motor command freshness. These three are independent monitoring systems for overlapping concerns. The integration document should define how they feed into a unified system health assessment.

3. **Update method naming**: Nodes use `process(uint64_t now)`. Drivers use `update()` (no timestamp parameter). The discrepancy means drivers cannot correlate their execution with the system clock without calling `esp_timer_get_time()` themselves. Consider passing the timestamp to `update()` as well.

### 5.3 Inconsistent MAX_* Constants

All subsystem MAX_* constants should be defined in one place:

```cpp
// chopper_limits.h
namespace chopper::limits {
    constexpr size_t MAX_NODES = 32;
    constexpr size_t MAX_TOPICS = 32;
    constexpr size_t MAX_SUBSCRIBERS_PER_TOPIC = 8;
    constexpr size_t MAX_MOTORS = 16;
    constexpr size_t MAX_CONTROLLERS = 4;
    constexpr size_t MAX_TIMERS = 16;
    constexpr size_t MAX_PARAMETERS = 64;
    constexpr size_t MAX_SERVICES = 8;
    constexpr size_t MAX_DRIVERS = 12;
    constexpr size_t MAX_TASKS = 8;
}
```

---

## 6. Summary of Action Items for Integration Document

| # | Category | Issue | Resolution |
|---|----------|-------|------------|
| 1 | **Timing** | Loop frequency mismatch (1 kHz vs 100 Hz) | Adopt multi-rate executor: 1 kHz safety/messaging, 100 Hz HAL drivers via node frequency |
| 2 | **Timing** | Serial TX time exceeds node WCET budgets | Stagger motor commands across ticks; migrate to non-blocking serial |
| 3 | **Timing** | Timer callback budget unallocated | Fold timers into node execution phase |
| 4 | **Memory** | Combined budget ~38 KB framework + ~130-185 KB system | Feasible for BT-only; WiFi adds pressure; reduce PenumbraCommDriver buffers |
| 5 | **Memory** | PenumbraCommDriver at 7.5 KB | Reduce buffer sizes to ~1.5 KB |
| 6 | **Config** | Dual NVS access (ParameterServer vs NvsConfig) | Unify: ParameterServer is the API, NvsConfig is internal |
| 7 | **Architecture** | HAL drivers not integrated into node graph | Define HAL bridge nodes (MotorDriverNode, ServoDriverNode, etc.) |
| 8 | **Safety** | Three independent e-stop mechanisms | Define single e-stop chain with clear call order |
| 9 | **Safety** | MotorSafety and DriverManager dual ownership of motors | MotorSafetyMonitor is authoritative for safety; DriverManager handles lifecycle |
| 10 | **Safety** | HAL errors not connected to ErrorLog | DriverManager writes to ErrorLog on state transitions |
| 11 | **Lifecycle** | Shutdown order undefined across subsystems | Define: e-stop msgs -> motor safety -> node deactivation -> driver shutdown -> broker shutdown |
| 12 | **Naming** | Enum style, callback types, string types inconsistent | Standardize: kPrefix enums, function pointers + void* ctx, const char* |
| 13 | **Naming** | Priority direction (higher=more important vs lower=more important) | Standardize: higher number = higher priority everywhere |
| 14 | **Architecture** | TimerManager ownership unclear (broker vs executor) | Executor owns TimerManager |
| 15 | **Architecture** | MAX_* constants scattered across documents | Centralize in chopper_limits.h |
| 16 | **Architecture** | Health monitoring in three parallel systems | Define unified health aggregator that feeds degradation mode decisions |
| 17 | **Stack** | Executor stack size 4 KB (current) vs 8 KB (recommended) | Use 8 KB, measure with StackMonitor, optimize later |
