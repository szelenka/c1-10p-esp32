# Safety and Real-Time Execution Design

## 1. Overview

This document specifies the safety hierarchy, real-time execution guarantees, resource monitoring, error propagation, and recovery procedures for the Chopper framework on ESP32. The design enforces the principle that **no software failure should cause uncontrolled mechanical motion**.

### Design Principles

1. **Defense in depth** -- four independent safety layers, each sufficient to stop the robot alone.
2. **Fail-safe defaults** -- every actuator output defaults to zero/stopped when its watchdog expires.
3. **Deterministic timing** -- worst-case execution time (WCET) is bounded and monitored for every node.
4. **No heap in critical path** -- all safety code uses static allocation; ring-buffer logging with no `malloc`.
5. **Graceful degradation** -- the system sheds non-critical functions to preserve safety-critical ones.

---

## 2. Current State Analysis

### Existing Code

| File | What it provides |
|------|-----------------|
| `Executor.h` | FreeRTOS task, 1 kHz loop, watchdog timeout, emergency stop callback, statistics |
| `Node.h` | State machine (INACTIVE/ACTIVE/PAUSED/ERROR/SHUTDOWN), per-node max execution time |
| `MotorSafety.h` | WPILib-derived per-motor watchdog with `Feed()`/`Check()` pattern, static `CheckMotors()` |
| `RobotDriveBase.h` | Drive base with deadband, ramping, speed limit, inherits MotorSafety |
| `DEVELOPMENT_PLAN.md` | Calls for multiple watchdog systems, graceful degradation, real-time constraints |

### Identified Gaps

| Gap | Impact |
|-----|--------|
| No hardware watchdog (ESP32 TWDT/IWDT) integration | Software hang leaves actuators powered |
| No multi-level safety hierarchy | Single e-stop path, no graduated response |
| No brown-out detection | Low voltage can cause erratic servo behavior |
| No stack/heap monitoring | Silent corruption from overflow |
| No deterministic timing analysis | Missed deadlines detected but not prevented |
| MotorSafety uses `wpi::mutex` | Non-FreeRTOS mutex, priority inversion risk |
| No ring-buffer error log | Errors go to serial (blocking I/O in safety path) |
| No recovery procedures | E-stop is terminal; no path back to operation |
| `std::string` in emergency stop path | Heap allocation in safety-critical code |

---

## 3. Safety Hierarchy

### 3.1 Four-Layer Architecture

```
Layer 0: HARDWARE WATCHDOG           [Cannot be disabled by software]
    |     ESP32 Task WDT (TWDT)
    |     Independent WDT (RTC)
    |     Brown-out detector (BOD)
    |
Layer 1: MOTOR SAFETY WATCHDOGS     [Per-actuator, independent timers]
    |     Each motor/servo has its own feed-or-stop timer
    |     Operates even if executor is hung
    |
Layer 2: CONTROLLER DISCONNECT      [Communication-level safety]
    |     BT link monitor with configurable timeout
    |     Multi-controller failover
    |     Heartbeat validation
    |
Layer 3: SOFTWARE RECOVERY          [Application-level safety]
          Node health monitoring
          Graceful degradation
          Automatic restart of failed nodes
```

### 3.2 Layer Interaction State Machine

```
                    +------------------+
                    |     RUNNING      |
                    | All layers green |
                    +--------+---------+
                             |
              +--------------+--------------+
              |              |              |
              v              v              v
     +--------+----+  +-----+------+  +----+-------+
     |  DEGRADED   |  | CONTROLLER |  |   MOTOR    |
     | Node failed |  |   LOST     |  |  TIMEOUT   |
     | Non-critical|  | BT timeout |  | Feed missed|
     +--------+----+  +-----+------+  +----+-------+
              |              |              |
              |              v              v
              |        +-----+------+  +---+--------+
              |        |  SAFE STOP |  | ACTUATOR   |
              |        | All motors |  |  DISABLED  |
              +------->|   stop     |<-| Per-motor  |
                       +-----+------+  +---+--------+
                             |
                             v
                    +--------+---------+
                    |  EMERGENCY STOP  |
                    | HW WDT / Manual  |
                    | All outputs off  |
                    +--------+---------+
                             |
                             v
                    +--------+---------+
                    | RECOVERY / RESET |
                    | Requires human   |
                    | action or auto   |
                    +------------------+
```

---

## 4. Layer 0: Hardware Watchdog Integration

### 4.1 ESP32 Task Watchdog Timer (TWDT)

The TWDT monitors FreeRTOS tasks and triggers a reset if a task fails to report in time.

```cpp
class HardwareWatchdog {
public:
    struct Config {
        uint32_t twdt_timeout_ms  = 3000;   // Task watchdog timeout
        bool     twdt_panic       = true;    // Reset on TWDT timeout
        bool     enable_bod       = true;    // Brown-out detection
        float    bod_threshold_v  = 3.0f;    // Brown-out voltage threshold
    };

    /// Initialize hardware watchdogs. Call once at boot.
    static bool initialize(const Config& config) {
        // Configure TWDT
        esp_task_wdt_config_t twdt_config = {
            .timeout_ms = config.twdt_timeout_ms,
            .idle_core_mask = 0,  // don't watch idle tasks
            .trigger_panic = config.twdt_panic,
        };
        esp_task_wdt_reconfigure(&twdt_config);
        return true;
    }

    /// Subscribe the current FreeRTOS task to the TWDT
    static bool subscribeCurrentTask() {
        return esp_task_wdt_add(NULL) == ESP_OK;
    }

    /// Feed the hardware watchdog (call from executor main loop)
    static void feed() {
        esp_task_wdt_reset();
    }

    /// Check if last reset was due to watchdog
    static bool wasWatchdogReset() {
        esp_reset_reason_t reason = esp_reset_reason();
        return reason == ESP_RST_TASK_WDT || reason == ESP_RST_INT_WDT;
    }
};
```

### 4.2 Brown-Out Detection

```cpp
class BrownOutDetector {
public:
    using Callback = void(*)(float voltage, void* context);

    static bool initialize(float threshold_v, Callback cb, void* ctx);

    /// Read current supply voltage via ADC
    static float readVoltage();

    /// Check if voltage is below threshold
    static bool isBrownOut();

    /// Get minimum recorded voltage since boot
    static float getMinVoltage();

private:
    static float    threshold_v_;
    static float    min_voltage_;
    static Callback callback_;
    static void*    context_;
};
```

### 4.3 Brown-Out Response

```
Voltage Level        Action
----------------------------------------------------------------------
> 3.3V               Normal operation
3.0V - 3.3V          WARNING: Log event, increase watchdog frequency
2.7V - 3.0V          DEGRADED: Disable non-critical nodes (LED, audio)
< 2.7V               EMERGENCY: Stop all motors, save state to NVS
< 2.5V               ESP32 hardware BOD triggers automatic reset
```

---

## 5. Layer 1: Motor Safety Watchdogs

### 5.1 Redesigned MotorSafety

Replace WPILib's mutex-based design with a FreeRTOS-native, ISR-safe implementation.

```cpp
class MotorSafetyMonitor {
public:
    static constexpr size_t MAX_MOTORS = 16;

    struct MotorEntry {
        uint8_t     id;
        const char* name;             // flash string
        uint32_t    timeout_us;       // watchdog timeout
        uint64_t    last_feed_us;     // last feed timestamp
        bool        enabled;
        bool        timed_out;        // latched until explicit reset
        bool        active;           // slot in use

        // Statistics
        uint32_t    timeout_count;
        uint32_t    feed_count;
        uint64_t    max_feed_interval_us;
    };

    using StopCallback = void(*)(uint8_t motor_id, void* context);

    /// Register a motor with its stop callback
    uint8_t registerMotor(const char* name, uint32_t timeout_ms,
                          StopCallback stop_cb, void* ctx);

    /// Unregister a motor
    void unregisterMotor(uint8_t id);

    /// Feed the watchdog for a specific motor (call on every command)
    void feed(uint8_t motor_id) {
        if (motor_id < MAX_MOTORS && motors_[motor_id].active) {
            uint64_t now = esp_timer_get_time();
            uint64_t interval = now - motors_[motor_id].last_feed_us;
            if (interval > motors_[motor_id].max_feed_interval_us) {
                motors_[motor_id].max_feed_interval_us = interval;
            }
            motors_[motor_id].last_feed_us = now;
            motors_[motor_id].feed_count++;
        }
    }

    /// Check all motors for timeout (called by executor every loop)
    void checkAll(uint64_t now_us) {
        for (size_t i = 0; i < MAX_MOTORS; ++i) {
            if (!motors_[i].active || !motors_[i].enabled) continue;
            uint64_t elapsed = now_us - motors_[i].last_feed_us;
            if (elapsed > motors_[i].timeout_us && !motors_[i].timed_out) {
                motors_[i].timed_out = true;
                motors_[i].timeout_count++;
                stop_callbacks_[i].callback(i, stop_callbacks_[i].context);
                ErrorLog::getInstance().log(
                    ErrorLog::MOTOR_TIMEOUT, i, elapsed);
            }
        }
    }

    /// Reset a timed-out motor (requires explicit action)
    bool resetMotor(uint8_t motor_id);

    /// Get motor status
    const MotorEntry* getMotor(uint8_t motor_id) const;

    /// Disable all motors immediately
    void disableAll();

private:
    MotorEntry motors_[MAX_MOTORS] = {};

    struct StopCallbackEntry {
        StopCallback callback;
        void*        context;
    };
    StopCallbackEntry stop_callbacks_[MAX_MOTORS] = {};
};
```

### 5.2 Timeout Configuration

| Actuator Type | Default Timeout | Rationale |
|--------------|-----------------|-----------|
| Drive motors | 100 ms | Fast stop on controller loss; 10 update cycles at 100 Hz |
| Dome motor | 200 ms | Dome has inertia, slightly longer acceptable |
| Servos (arms, panels) | 500 ms | Servo holds position naturally; timeout just disables PWM |
| Continuous servos | 100 ms | Like motors -- must stop on timeout |
| LED controllers | 2000 ms | Non-safety; aesthetic-only timeout |
| Audio | No timeout | Audio is not safety-critical |

---

## 6. Layer 2: Controller Disconnect Detection

### 6.1 Connection Monitor

```cpp
class ControllerConnectionMonitor {
public:
    struct Config {
        uint32_t heartbeat_timeout_ms  = 500;   // Max time between valid packets
        uint32_t disconnect_timeout_ms = 1000;  // Declare disconnect after this
        uint32_t reconnect_grace_ms    = 2000;  // Allow reconnect within this window
        uint8_t  min_rssi              = -80;    // Minimum signal strength (dBm)
        bool     require_heartbeat     = true;
    };

    enum class State {
        DISCONNECTED,
        CONNECTING,
        CONNECTED,
        SIGNAL_WEAK,
        PACKET_LOSS,
        GRACE_PERIOD    // recently disconnected, allowing reconnect
    };

    /// Update with latest controller data (call from BT callback)
    void onControllerData(uint8_t controller_id, int8_t rssi,
                          uint64_t timestamp_us);

    /// Check connection health (call from executor)
    State checkConnection(uint8_t controller_id, uint64_t now_us);

    /// Get time since last valid packet
    uint32_t getTimeSinceLastPacket(uint8_t controller_id) const;

    /// Get current RSSI
    int8_t getRSSI(uint8_t controller_id) const;

private:
    static constexpr size_t MAX_CONTROLLERS = 4;

    struct ControllerState {
        uint64_t last_packet_us;
        uint64_t disconnect_time_us;
        int8_t   rssi;
        State    state;
        uint32_t packet_count;
        uint32_t missed_packets;
        bool     active;
    };

    ControllerState controllers_[MAX_CONTROLLERS] = {};
    Config          config_;
};
```

### 6.2 Disconnect Response Sequence

```
Time 0ms:     Last valid BT packet received
Time 200ms:   SIGNAL_WEAK: reduce max speed to 50%
Time 500ms:   PACKET_LOSS: begin motor ramp-down
Time 1000ms:  DISCONNECTED: all motors stopped, enter GRACE_PERIOD
Time 3000ms:  Grace expired: full SAFE_STOP, require manual restart
              OR controller reconnects -> resume from GRACE_PERIOD
```

State machine:

```
CONNECTED ──timeout 200ms──> SIGNAL_WEAK ──timeout 500ms──> PACKET_LOSS
    ^                              |                             |
    |                              |  packet received            |  timeout 1000ms
    |                              v                             v
    +─────── packet received ─── CONNECTED                 DISCONNECTED
                                                               |
                                                               | timeout 2000ms
                                                               v
                                                          GRACE_PERIOD
                                                               |
                                           packet received     |  timeout
                                               |               v
                                               v           SAFE_STOP
                                           CONNECTED      (requires reset)
```

---

## 7. Layer 3: Software Recovery

### 7.1 Node Health Monitor

```cpp
class NodeHealthMonitor {
public:
    enum class HealthStatus {
        HEALTHY,
        WARNING,          // execution time approaching limit
        OVERRUN,          // deadline missed
        STALLED,          // node not responding
        CRASHED,          // exception caught
        DISABLED          // manually or automatically disabled
    };

    struct NodeHealth {
        uint8_t        node_index;
        HealthStatus   status;
        uint32_t       consecutive_overruns;
        uint32_t       total_overruns;
        uint64_t       last_execution_us;
        uint64_t       max_execution_us;
        uint64_t       avg_execution_us;    // EMA
        bool           is_critical;         // safety-critical node
        uint8_t        restart_count;
    };

    /// Update node health after each execution cycle
    void recordExecution(uint8_t node_index, uint64_t execution_time_us,
                         uint64_t deadline_us, bool success);

    /// Get degradation action for current health
    enum class Action {
        NONE,
        LOG_WARNING,
        REDUCE_FREQUENCY,
        RESTART_NODE,
        DISABLE_NODE,
        EMERGENCY_STOP
    };

    Action getAction(uint8_t node_index) const;

private:
    static constexpr size_t MAX_NODES = 32;
    NodeHealth nodes_[MAX_NODES] = {};

    // Thresholds
    static constexpr uint32_t WARN_OVERRUNS = 3;
    static constexpr uint32_t RESTART_OVERRUNS = 10;
    static constexpr uint32_t DISABLE_OVERRUNS = 25;
    static constexpr uint32_t MAX_RESTARTS = 3;
};
```

### 7.2 Graceful Degradation Modes

The system defines five operational modes. Transitions are automatic based on system health.

```
+-------------------------------------------------------------+
|  Mode 0: FULL OPERATION                                     |
|  All nodes active, all features enabled                     |
+-----+-------------------------------------------------------+
      |  Non-critical node failure
      v
+-----+-------------------------------------------------------+
|  Mode 1: REDUCED FEATURES                                   |
|  Disable: LED animations, audio effects, telemetry logging  |
|  Active:  Drive, dome, servos, controller input, safety     |
+-----+-------------------------------------------------------+
      |  Controller signal weak / executor overload
      v
+-----+-------------------------------------------------------+
|  Mode 2: ESSENTIAL ONLY                                     |
|  Disable: Dome, servos, animations                          |
|  Active:  Drive (reduced speed), controller, safety         |
+-----+-------------------------------------------------------+
      |  Controller lost / critical node failure
      v
+-----+-------------------------------------------------------+
|  Mode 3: SAFE STOP                                          |
|  All actuators commanded to stop/neutral                    |
|  Waiting for controller reconnect or manual intervention    |
+-----+-------------------------------------------------------+
      |  Hardware watchdog / unrecoverable error
      v
+-----+-------------------------------------------------------+
|  Mode 4: EMERGENCY STOP                                     |
|  All outputs disabled at hardware level                     |
|  Requires power cycle or explicit reset command             |
+-------------------------------------------------------------+
```

### 7.3 Node Criticality Classification

| Node | Critical | Degradation Behavior |
|------|----------|---------------------|
| SafetyMonitorNode | YES | Cannot be disabled; triggers Mode 4 if it fails |
| DriveNode | YES | Reduced speed in Mode 2; stopped in Mode 3 |
| ControllerInputNode | YES | Mode 3 if all controllers lost |
| DomeNode | NO | Disabled in Mode 2 |
| ServoNode | NO | Disabled in Mode 2 |
| AudioNode | NO | Disabled in Mode 1 |
| LEDNode | NO | Disabled in Mode 1 |
| TelemetryNode | NO | Disabled in Mode 1 |

---

## 8. Deterministic Timing Analysis

### 8.1 Executor Timing Budget

At 1 kHz loop rate, each loop has 1000 us total budget.

```
+------------------------------------------------------------------+
| 1000 us total loop budget                                        |
|                                                                  |
| [SAFETY: 50us] [MSGS: 200us] [NODES: 600us] [STATS: 50us] [100]|
|                                                                  |
| SAFETY  = Hardware WDT feed + motor safety check + BOD check    |
| MSGS    = Priority message dispatch budget                       |
| NODES   = Node process() calls (sum of all active nodes)        |
| STATS   = Update statistics, check degradation                   |
| RESERVE = 100 us slack for jitter / interrupts                   |
+------------------------------------------------------------------+
```

### 8.2 Per-Node WCET Budget

```cpp
struct NodeTimingConfig {
    uint32_t    wcet_us;              // worst-case execution time budget
    uint32_t    period_us;            // execution period
    uint8_t     priority;             // scheduling priority
    bool        hard_deadline;        // true = overrun triggers safety action
};
```

| Node | WCET Budget | Period | Priority | Hard Deadline |
|------|------------|--------|----------|---------------|
| SafetyMonitorNode | 30 us | 1 ms | CRITICAL | YES |
| ControllerInputNode | 50 us | 20 ms | HIGH | YES |
| DriveNode | 80 us | 20 ms | HIGH | YES |
| DomeNode | 60 us | 50 ms | NORMAL | NO |
| ServoNode | 40 us | 50 ms | NORMAL | NO |
| AudioNode | 30 us | 100 ms | LOW | NO |
| LEDNode | 40 us | 100 ms | LOW | NO |
| TelemetryNode | 50 us | 1000 ms | BACKGROUND | NO |

### 8.3 Timing Enforcement

```cpp
class TimingEnforcer {
public:
    /// Called before each node's process()
    void startMeasurement(uint8_t node_index) {
        start_time_[node_index] = esp_timer_get_time();
    }

    /// Called after each node's process()
    /// Returns true if node stayed within budget
    bool endMeasurement(uint8_t node_index, uint32_t budget_us) {
        uint64_t elapsed = esp_timer_get_time() - start_time_[node_index];
        recordTiming(node_index, elapsed);

        if (elapsed > budget_us) {
            handleOverrun(node_index, elapsed, budget_us);
            return false;
        }

        // Warn if approaching budget (>80%)
        if (elapsed > (budget_us * 4 / 5)) {
            ErrorLog::getInstance().log(
                ErrorLog::TIMING_WARNING, node_index,
                static_cast<uint32_t>(elapsed));
        }
        return true;
    }

private:
    uint64_t start_time_[MAX_NODES];

    struct TimingRecord {
        uint64_t min_us;
        uint64_t max_us;
        uint64_t avg_us;       // EMA alpha=1/16
        uint32_t overruns;
        uint32_t total_calls;
    };
    TimingRecord records_[MAX_NODES];

    void recordTiming(uint8_t node_index, uint64_t elapsed_us) {
        auto& r = records_[node_index];
        if (elapsed_us < r.min_us) r.min_us = elapsed_us;
        if (elapsed_us > r.max_us) r.max_us = elapsed_us;
        r.avg_us = r.avg_us - (r.avg_us >> 4) + (elapsed_us >> 4);
        r.total_calls++;
    }

    void handleOverrun(uint8_t node_index, uint64_t elapsed, uint32_t budget);
};
```

### 8.4 Rate-Monotonic Scheduling Analysis

For single-core ESP32, worst-case CPU utilization must be below the schedulability bound:

```
U = sum(C_i / T_i) for all nodes

Where:
  C_i = WCET of node i
  T_i = Period of node i

Calculation:
  Safety:     30/1000   = 0.030
  Controller: 50/20000  = 0.0025
  Drive:      80/20000  = 0.004
  Dome:       60/50000  = 0.0012
  Servo:      40/50000  = 0.0008
  Audio:      30/100000 = 0.0003
  LED:        40/100000 = 0.0004
  Telemetry:  50/1000000= 0.00005
  ---------------------------------
  Total U = 0.039 (3.9% CPU utilization)

  For 8 tasks, RM bound = 8 * (2^(1/8) - 1) = 0.724 (72.4%)

  0.039 << 0.724 --> system is easily schedulable
```

The remaining ~96% CPU budget provides ample headroom for:
- Message broker dispatch (budgeted 200 us/loop)
- BT stack processing (runs on core 0)
- FreeRTOS overhead
- Future node additions

---

## 9. Stack and Heap Monitoring

### 9.1 Stack Monitor

```cpp
class StackMonitor {
public:
    struct TaskStackInfo {
        const char*   task_name;
        uint32_t      stack_size;
        uint32_t      high_water_mark;  // minimum free stack ever
        uint32_t      current_free;
        float         usage_percent;
        bool          warning;          // >80% usage
    };

    /// Check stack usage for all monitored tasks
    void checkAll() {
        for (size_t i = 0; i < task_count_; ++i) {
            if (task_handles_[i] == nullptr) continue;
            uint32_t hwm = uxTaskGetStackHighWaterMark(task_handles_[i]);
            stacks_[i].high_water_mark = hwm;
            stacks_[i].usage_percent =
                100.0f * (1.0f - (float)hwm / stacks_[i].stack_size);

            if (stacks_[i].usage_percent > 80.0f && !stacks_[i].warning) {
                stacks_[i].warning = true;
                ErrorLog::getInstance().log(
                    ErrorLog::STACK_WARNING, i,
                    static_cast<uint32_t>(stacks_[i].usage_percent));
            }
        }
    }

    /// Register a FreeRTOS task for monitoring
    void registerTask(TaskHandle_t handle, const char* name, uint32_t stack_size);

    /// Get stack info for a task
    const TaskStackInfo* getInfo(size_t index) const;

    /// Get number of monitored tasks
    size_t getTaskCount() const { return task_count_; }

private:
    static constexpr size_t MAX_TASKS = 8;
    TaskHandle_t   task_handles_[MAX_TASKS] = {};
    TaskStackInfo  stacks_[MAX_TASKS] = {};
    size_t         task_count_ = 0;
};
```

### 9.2 Heap Monitor

```cpp
class HeapMonitor {
public:
    struct HeapInfo {
        size_t   total_bytes;
        size_t   free_bytes;
        size_t   min_free_ever;
        size_t   largest_free_block;
        uint32_t alloc_count;         // total allocations since boot
        uint32_t free_count;          // total frees since boot
        float    fragmentation;       // 1.0 - (largest_block / total_free)
    };

    /// Sample current heap state
    HeapInfo sample() const {
        HeapInfo info;
        info.total_bytes       = heap_caps_get_total_size(MALLOC_CAP_DEFAULT);
        info.free_bytes        = heap_caps_get_free_size(MALLOC_CAP_DEFAULT);
        info.min_free_ever     = heap_caps_get_minimum_free_size(MALLOC_CAP_DEFAULT);
        info.largest_free_block= heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT);
        info.fragmentation     = 1.0f - (float)info.largest_free_block / info.free_bytes;
        return info;
    }

    /// Check if heap is healthy
    bool isHealthy() const {
        auto info = sample();
        return info.free_bytes > MIN_FREE_HEAP &&
               info.largest_free_block > MIN_LARGEST_BLOCK;
    }

    /// Heap thresholds
    static constexpr size_t MIN_FREE_HEAP = 16384;       // 16 KB
    static constexpr size_t MIN_LARGEST_BLOCK = 8192;     // 8 KB
    static constexpr float  MAX_FRAGMENTATION = 0.5f;
};
```

### 9.3 Recommended Stack Sizes

| FreeRTOS Task | Stack Size | Rationale |
|--------------|-----------|-----------|
| Executor (main loop) | 8192 bytes | Runs all nodes, needs headroom |
| BT stack (core 0) | 4096 bytes | Bluepad32 requirement |
| Serial/debug shell | 4096 bytes | String formatting |
| Web server (if enabled) | 8192 bytes | HTTP parsing, JSON generation |
| NVS writer | 2048 bytes | Flash write operations |

---

## 10. Error Propagation and Logging

### 10.1 Ring-Buffer Error Log (No Heap)

```cpp
class ErrorLog {
public:
    static constexpr size_t LOG_SIZE = 128;  // number of entries

    enum Category : uint8_t {
        MOTOR_TIMEOUT     = 0x01,
        TIMING_WARNING    = 0x02,
        TIMING_OVERRUN    = 0x03,
        STACK_WARNING     = 0x04,
        HEAP_WARNING      = 0x05,
        BROWNOUT          = 0x06,
        CONTROLLER_LOST   = 0x07,
        NODE_CRASH        = 0x08,
        SAFETY_ESTOP      = 0x09,
        WATCHDOG_RESET    = 0x0A,
        DEGRADATION       = 0x0B,
        NODE_RESTART      = 0x0C,
        PARAM_ERROR       = 0x0D,
        BUS_ERROR         = 0x0E,
        GENERIC_WARNING   = 0xFE,
        GENERIC_ERROR     = 0xFF,
    };

    enum Severity : uint8_t {
        DEBUG   = 0,
        INFO    = 1,
        WARNING = 2,
        ERROR   = 3,
        FATAL   = 4,
    };

    struct Entry {
        uint64_t  timestamp_us;
        Category  category;
        Severity  severity;
        uint8_t   source_id;      // node index or motor index
        uint32_t  detail;         // category-specific value
        // 16 bytes per entry -> 2 KB total for 128 entries
    };

    static ErrorLog& getInstance();

    /// Log an entry (lock-free, ISR-safe)
    void log(Category cat, uint8_t source, uint32_t detail,
             Severity sev = Severity::ERROR) {
        size_t idx = write_index_.fetch_add(1, std::memory_order_relaxed) % LOG_SIZE;
        entries_[idx].timestamp_us = esp_timer_get_time();
        entries_[idx].category = cat;
        entries_[idx].severity = sev;
        entries_[idx].source_id = source;
        entries_[idx].detail = detail;
        entry_count_.fetch_add(1, std::memory_order_relaxed);
    }

    /// Read entries (for debug shell / web UI)
    /// Returns number of entries copied
    size_t readRecent(Entry* out, size_t max_count) const;

    /// Get total error count since boot
    uint32_t getTotalCount() const {
        return entry_count_.load(std::memory_order_relaxed);
    }

    /// Get count by severity
    uint32_t getCountBySeverity(Severity sev) const;

    /// Clear the log
    void clear();

    /// Persist log to NVS (call on graceful shutdown)
    void persistToNVS();

    /// Load log from NVS (call on boot to see pre-reset errors)
    void loadFromNVS();

private:
    ErrorLog() = default;
    Entry entries_[LOG_SIZE];
    std::atomic<size_t> write_index_{0};
    std::atomic<uint32_t> entry_count_{0};
};
```

### 10.2 Error Log Memory Layout

```
Total: 128 entries x 16 bytes = 2,048 bytes (2 KB)

Each entry:
+--------+--------+--------+--------+--------+--------+
| bytes  |  0-7   |   8    |   9    |   10   | 11-14  |
+--------+--------+--------+--------+--------+--------+
| field  | tstamp | categ  |  sev   | src_id | detail |
+--------+--------+--------+--------+--------+--------+

Note: 1 byte padding at offset 15 for alignment
```

### 10.3 Debug Shell Error Display

```
> error list --last 10
  TIME          SEV   CATEGORY         SOURCE  DETAIL
  12045.230ms   WARN  TIMING_WARNING   Node#2  820us (budget 1000us)
  12100.500ms   ERR   MOTOR_TIMEOUT    Motor#0 1200us since feed
  12100.510ms   ERR   CONTROLLER_LOST  Ctrl#0  1050ms no packet
  12100.520ms   INFO  DEGRADATION      System  Mode 1 -> Mode 3

> error stats
  Total entries: 47
  DEBUG: 12  INFO: 15  WARNING: 14  ERROR: 5  FATAL: 1
  Most common: TIMING_WARNING (14 occurrences)
  Last FATAL: 8032.100ms SAFETY_ESTOP source=System
```

---

## 11. Recovery Procedures

### 11.1 Recovery State Machine

```cpp
class RecoveryManager {
public:
    enum class RecoveryState {
        NORMAL,
        ASSESSING,          // evaluating what failed
        RECOVERING,         // attempting automatic recovery
        WAITING_FOR_INPUT,  // needs human action (controller reconnect)
        RECOVERED,          // back to normal operation
        UNRECOVERABLE       // requires power cycle
    };

    /// Attempt recovery from current fault condition
    RecoveryState attemptRecovery(DegradationMode current_mode);

    /// Check if system can return to a higher operational mode
    bool canUpgrade(DegradationMode target_mode);

    /// Execute upgrade to higher operational mode
    bool upgrade(DegradationMode target_mode);

    /// Get current recovery state
    RecoveryState getState() const { return state_; }

private:
    RecoveryState state_ = RecoveryState::NORMAL;
    uint8_t       recovery_attempts_ = 0;
    uint64_t      last_recovery_us_ = 0;

    static constexpr uint8_t  MAX_RECOVERY_ATTEMPTS = 3;
    static constexpr uint32_t RECOVERY_COOLDOWN_MS = 5000;
};
```

### 11.2 Recovery Procedures by Fault Type

| Fault | Automatic Recovery | Manual Recovery |
|-------|-------------------|-----------------|
| Single node crash (non-critical) | Restart node (up to 3 times), then disable | Re-enable via debug shell |
| Controller disconnect | Wait in GRACE_PERIOD for reconnect | Press controller button to reconnect |
| Motor timeout | Reset motor watchdog when feed resumes | Send reset command via debug shell |
| Timing overrun | Reduce node frequency, then disable | Adjust WCET budget via parameter |
| Brown-out warning | Shed non-critical nodes | Check/replace battery |
| Brown-out critical | Emergency stop, save state | Power cycle with good battery |
| Stack overflow | Not recoverable (corruption likely) | Power cycle, increase stack size |
| Hardware watchdog reset | System reboots, loads pre-crash log from NVS | Check error log after reboot |

### 11.3 Node Restart Procedure

```cpp
bool restartNode(uint8_t node_index) {
    NodePtr node = nodes_[node_index];
    NodeHealth& health = health_monitor_.getHealth(node_index);

    // Check restart budget
    if (health.restart_count >= MAX_RESTARTS) {
        ErrorLog::getInstance().log(ErrorLog::NODE_RESTART, node_index,
                                     health.restart_count, ErrorLog::ERROR);
        return false;  // too many restarts, disable instead
    }

    // 1. Deactivate
    node->deactivate();

    // 2. Wait one executor cycle for cleanup
    //    (handled by setting state to PENDING_RESTART)

    // 3. Re-initialize
    if (!node->initialize()) {
        ErrorLog::getInstance().log(ErrorLog::NODE_CRASH, node_index, 0);
        return false;
    }

    // 4. Re-activate
    if (!node->activate()) {
        ErrorLog::getInstance().log(ErrorLog::NODE_CRASH, node_index, 1);
        return false;
    }

    health.restart_count++;
    health.consecutive_overruns = 0;
    ErrorLog::getInstance().log(ErrorLog::NODE_RESTART, node_index,
                                 health.restart_count, ErrorLog::INFO);
    return true;
}
```

---

## 12. Revised Executor Design

### 12.1 Main Loop with Safety Integration

```cpp
void Executor::executionLoop() {
    HardwareWatchdog::subscribeCurrentTask();
    StackMonitor::getInstance().registerTask(
        xTaskGetCurrentTaskHandle(), "executor", config_.stack_size);

    while (!should_stop_) {
        uint64_t loop_start = esp_timer_get_time();

        // ---- Phase 0: Hardware watchdog feed (must be first) ----
        HardwareWatchdog::feed();

        // ---- Phase 1: Safety checks (50 us budget) ----
        {
            motor_safety_.checkAll(loop_start);
            connection_monitor_.checkAll(loop_start);

            if (BrownOutDetector::isBrownOut()) {
                handleBrownOut(BrownOutDetector::readVoltage());
            }

            // Check degradation triggers
            DegradationMode new_mode = evaluateDegradation();
            if (new_mode != current_mode_) {
                transitionMode(current_mode_, new_mode);
            }
        }

        // ---- Phase 2: Message dispatch (200 us budget) ----
        message_broker_.processPending(200);

        // ---- Phase 3: Timer callbacks ----
        timer_manager_.processDueTimers(esp_timer_get_time());

        // ---- Phase 4: Node execution (600 us budget) ----
        for (size_t i = 0; i < node_count_; ++i) {
            if (!shouldExecuteNode(i, loop_start)) continue;

            timing_enforcer_.startMeasurement(i);
            nodes_[i]->process(loop_start);
            bool ok = timing_enforcer_.endMeasurement(
                i, node_configs_[i].wcet_us);

            if (!ok) {
                NodeHealthMonitor::Action action =
                    health_monitor_.getAction(i);
                executeHealthAction(i, action);
            }
        }

        // ---- Phase 5: Statistics and monitoring (50 us budget) ----
        if (config_.enable_statistics) {
            uint64_t loop_time = esp_timer_get_time() - loop_start;
            updateStatistics(loop_time);

            // Periodic stack/heap check (every 1000 loops = 1 second)
            if ((stats_.loop_count & 0x3FF) == 0) {
                stack_monitor_.checkAll();
                if (!heap_monitor_.isHealthy()) {
                    ErrorLog::getInstance().log(
                        ErrorLog::HEAP_WARNING, 0,
                        heap_monitor_.sample().free_bytes);
                }
            }
        }

        // ---- Phase 6: Sleep until next loop ----
        uint64_t elapsed = esp_timer_get_time() - loop_start;
        if (elapsed < config_.loop_period_us) {
            vTaskDelay(pdMS_TO_TICKS(
                (config_.loop_period_us - elapsed) / 1000));
        } else {
            stats_.missed_deadlines++;
            taskYIELD();  // yield but don't sleep
        }
    }
}
```

### 12.2 Degradation Mode Transitions

```cpp
DegradationMode Executor::evaluateDegradation() {
    // Check from highest severity to lowest
    if (emergency_stop_) return DegradationMode::EMERGENCY_STOP;

    if (!connection_monitor_.hasAnyConnected()) {
        return DegradationMode::SAFE_STOP;
    }

    bool any_critical_failed = false;
    bool any_noncritical_failed = false;

    for (size_t i = 0; i < node_count_; ++i) {
        auto status = health_monitor_.getStatus(i);
        if (status >= NodeHealthMonitor::HealthStatus::CRASHED) {
            if (node_configs_[i].is_critical) {
                any_critical_failed = true;
            } else {
                any_noncritical_failed = true;
            }
        }
    }

    if (any_critical_failed) return DegradationMode::SAFE_STOP;

    if (connection_monitor_.isWeak() || isOverloaded()) {
        return DegradationMode::ESSENTIAL_ONLY;
    }

    if (any_noncritical_failed) return DegradationMode::REDUCED_FEATURES;

    return DegradationMode::FULL_OPERATION;
}
```

---

## 13. LED Status Indication

System state is communicated via a status LED (or NeoPixel):

| Mode | LED Pattern | Color | Description |
|------|------------|-------|-------------|
| FULL_OPERATION | Solid | Green | All systems nominal |
| REDUCED_FEATURES | Slow blink (1 Hz) | Yellow | Non-critical node disabled |
| ESSENTIAL_ONLY | Fast blink (2 Hz) | Orange | Reduced functionality |
| SAFE_STOP | Double flash | Red | Motors stopped, waiting |
| EMERGENCY_STOP | Rapid flash (4 Hz) | Red | Emergency stop active |
| BOOT/INIT | Breathing | Blue | System starting up |
| BT SEARCHING | Slow pulse | Purple | Waiting for controller |
| BROWN_OUT | Triple flash | Red/Yellow | Low voltage warning |

---

## 14. Memory Budget

| Component | Size | Notes |
|-----------|------|-------|
| MotorSafetyMonitor (16 motors) | 16 x 48 = 768 B | Per-motor state + callbacks |
| ControllerConnectionMonitor (4 ctrl) | 4 x 48 = 192 B | |
| NodeHealthMonitor (32 nodes) | 32 x 40 = 1,280 B | |
| TimingEnforcer (32 nodes) | 32 x 48 = 1,536 B | Includes timing records |
| ErrorLog (128 entries) | 128 x 16 = 2,048 B | Ring buffer, no heap |
| StackMonitor (8 tasks) | 8 x 32 = 256 B | |
| HeapMonitor | 32 B | Singleton state |
| RecoveryManager | 24 B | |
| BrownOutDetector | 16 B | |
| HardwareWatchdog | 8 B | Minimal state |
| **Total** | **~6.2 KB** | ~1.9% of 320 KB SRAM |

Combined with message system (13.8 KB), total framework overhead is ~20 KB (6.2% of SRAM).

---

## 15. ESP32-Specific Considerations

| Concern | Mitigation |
|---------|------------|
| TWDT is per-task, not per-node | Executor feeds TWDT; per-node monitoring is via TimingEnforcer |
| RTC WDT survives deep sleep | Used for brown-out recovery; saves state before sleep |
| BT stack on core 0 | Safety runs on core 1 (executor task); no cross-core locking needed |
| GPIO for motor enable | Hardware kill switch: GPIO connected to motor driver enable pins, pulled low on E-STOP |
| ADC for voltage sensing | One ADC channel reads battery voltage through voltage divider; sampled in safety phase |
| NVS write latency | ~10 ms for NVS write; only done on mode transitions, not in safety loop |
| ISR latency | ESP32 ISR latency ~2 us; BOD interrupt can trigger immediate motor disable |
| Brown-out reset | ESP32 BOD can be configured to reset; we catch the interrupt first to save state |
| PSRAM (if present) | Never used for safety-critical data; only for audio buffers or web content |

---

## 16. Boot Sequence with Safety

```
Power On
    |
    v
[1] Hardware init (GPIO, ADC, UART)
    |
    v
[2] Check reset reason
    |-- Watchdog reset?  -> Load crash log from NVS, log event
    |-- Brown-out reset? -> Check voltage before proceeding
    |-- Normal boot      -> Continue
    |
    v
[3] Initialize HardwareWatchdog (TWDT, BOD)
    |
    v
[4] Initialize ErrorLog, StackMonitor, HeapMonitor
    |
    v
[5] Initialize MotorSafetyMonitor (all motors disabled)
    |
    v
[6] Load parameters from NVS
    |
    v
[7] Initialize Executor + MessageBroker
    |
    v
[8] Create and initialize all Nodes
    |-- Each node registers its motors with MotorSafetyMonitor
    |
    v
[9] Activate nodes (safety-critical first)
    |
    v
[10] Wait for BT controller connection
    |-- System in Mode 3 (SAFE_STOP) until controller connects
    |
    v
[11] Controller connected -> transition to Mode 0 (FULL_OPERATION)
    |
    v
[12] Main executor loop running
```

---

## 17. Testing Strategy

### 17.1 Unit Tests (Host-Based)

| Test | What it validates |
|------|------------------|
| MotorSafetyMonitor timeout | Motors stop after timeout period |
| MotorSafetyMonitor feed | Feed resets timer correctly |
| NodeHealthMonitor thresholds | Correct action for consecutive overruns |
| ErrorLog ring wrap | Oldest entries overwritten correctly |
| ErrorLog ISR safety | Concurrent writes produce valid entries |
| TimingEnforcer overrun | Overrun detected at correct threshold |
| DegradationMode transitions | Correct mode for each fault combination |
| RecoveryManager restart | Node restart with attempt counting |

### 17.2 Integration Tests (On-Target)

| Test | What it validates |
|------|------------------|
| Full boot sequence | All components initialize without errors |
| Controller disconnect response | Motors stop within timeout + grace period |
| Brown-out simulation | System degrades and recovers at correct voltage levels |
| TWDT trigger | System resets and recovers from watchdog timeout |
| Stack overflow detection | Warning fires before actual overflow |
| Multi-node overrun | Degradation mode activates correctly |
| NVS crash log persistence | Error log survives reset and is readable after reboot |

---

## 18. Open Questions

1. **Hardware kill switch**: Should we require a physical E-STOP button wired to GPIO that directly cuts motor driver enable pins, independent of all software?
2. **Dual-core safety**: Should the safety monitor run on core 0 (alongside BT) as an independent watchdog of core 1 (executor)?
3. **Servo power cutoff**: Should we add a MOSFET to cut servo rail power on E-STOP, or is disabling PWM sufficient?
4. **Recovery authorization**: Should automatic recovery from SAFE_STOP require an explicit "resume" button press, or should controller reconnection be sufficient?
5. **Error log persistence**: Should the ring buffer auto-persist to NVS every N entries, or only on shutdown/fault? NVS has limited write cycles (~100K per sector).
6. **Voltage monitoring calibration**: The ADC voltage divider ratio and calibration offset should be configurable per-board. Should this be a build-time constant or a runtime parameter?
