# Hardware Abstraction Layer (HAL) Design

## 1. Overview

This document defines the Hardware Abstraction Layer for the chopper ESP32 astromech robot framework. The HAL provides a unified interface for all peripheral hardware -- motor controllers, servo controllers, audio, sensors, and inter-board communication -- while enabling testability through mock/simulation drivers, health monitoring, and runtime configuration via NVS.

### 1.1 Design Goals

- **Uniform driver lifecycle**: Every driver follows the same init/update/status/error pattern regardless of hardware type.
- **Bus-aware resource management**: UART ports are a scarce shared resource; the HAL explicitly manages allocation and contention.
- **Testability without hardware**: Mock drivers implement the same interfaces, enabling full logic testing on a host PC or ESP32 without physical peripherals.
- **Zero dynamic allocation on the hot path**: All driver state is pre-allocated at init; the periodic `update()` path uses only stack and pre-allocated buffers.
- **NVS-backed configuration**: Pin assignments, baud rates, calibration values, and per-driver tuning are stored in NVS and loaded at init, enabling runtime reconfiguration without recompilation.

### 1.2 Hardware Inventory

| Device                        | Bus       | Protocol          | Direction  | Baud Rate | Notes                                      |
|-------------------------------|-----------|-------------------|------------|-----------|--------------------------------------------|
| Sabertooth 2x32 (addr 129)   | UART (SW) | Packet Serial     | TX-only    | 9600      | Differential drive, 2 motors               |
| SyRen 10 (addr 128)          | UART (SW) | Packet Serial     | TX-only    | 9600      | Dome rotation, 1 motor                     |
| Pololu Maestro Body (id 12)  | UART (SW) | Pololu Protocol   | Bidi       | 9600      | 6 channels: neck servos, utility arm, doors|
| Pololu Maestro Dome (id 13)  | UART (SW) | Pololu Protocol   | Bidi       | 9600      | 11 channels: periscope, doors, arms        |
| SparkFun MP3 Trigger          | UART (SW) | Serial commands   | Bidi       | 38400     | Audio playback                             |
| OpenMV Camera                 | UART (HW) | Custom/Serial     | Bidi       | 115200    | Vision processing (Serial2)                |
| Dome Potentiometer            | ADC       | Analog            | Input      | N/A       | GPIO34, 12-bit ADC                         |
| Penumbra Board                | UART      | MessageHandler     | Bidi       | TBD       | LED commands, inter-board messaging         |
| LEDs (front/back)             | GPIO      | Digital out       | Output     | N/A       | GPIO27 (shared pin currently)              |

---

## 2. Driver Interface Hierarchy

```
IDriver (abstract)
  Common lifecycle for ALL hardware drivers.
  Every peripheral in the system implements this interface.
  |
  +-- init() -> DriverStatus        // One-time initialization (configure pins, baud, etc.)
  +-- update() -> void              // Called each executor cycle (send/receive data)
  +-- getStatus() -> DriverStatus   // Current operational state
  +-- getErrorState() -> ErrorInfo  // Last error code + human-readable message
  +-- getName() -> const char*      // Driver identifier for diagnostics
  +-- reset() -> DriverStatus       // Re-initialize without full system restart
  +-- shutdown() -> void            // Graceful power-down / disable outputs
  |
  +------- IMotorDriver (abstract)
  |          Extends IDriver for DC motor control.
  |          |
  |          +-- set(float speed) -> void               // [-1.0 .. 1.0]
  |          +-- get() -> float                         // Current setpoint
  |          +-- setInverted(bool) -> void
  |          +-- isInverted() -> bool
  |          +-- disable() -> void                      // Coast / Hi-Z
  |          +-- stop() -> void                         // Active brake
  |          |
  |          +------- SabertoothMotorDriver
  |          |          Wraps Sabertooth library for a single motor channel.
  |          |          Implements Packet Serial TX-only protocol.
  |          |          One instance per motor (e.g., left foot, right foot, dome).
  |          |
  |          +------- MockMotorDriver
  |                     Logs set/get calls to a buffer. No hardware access.
  |                     Returns last-set speed from get(). Always reports kReady.
  |
  +------- IServoController (abstract)
  |          Extends IDriver for multi-channel PWM servo control.
  |          |
  |          +-- setPosition(uint8_t channel, uint16_t pulse_us) -> void
  |          +-- setAngle(uint8_t channel, float angle) -> void
  |          +-- getPosition(uint8_t channel) -> uint16_t
  |          +-- enable(uint8_t channel) -> void
  |          +-- disable(uint8_t channel) -> void
  |          +-- disableAll() -> void
  |          +-- setSpeed(uint8_t channel, uint16_t speed) -> void
  |          +-- setAcceleration(uint8_t channel, uint16_t accel) -> void
  |          +-- getChannelCount() -> uint8_t
  |          |
  |          +------- MaestroServoDriver
  |          |          Wraps PololuMaestro library (MiniMaestro).
  |          |          Uses setMultiTarget for batched updates.
  |          |          One instance per physical Maestro board.
  |          |
  |          +------- MockServoController
  |                     Stores positions in array. No hardware access.
  |                     Logs all position changes for test assertions.
  |
  +------- IAudioDriver (abstract)
  |          Extends IDriver for audio playback.
  |          |
  |          +-- trigger(uint8_t track) -> void
  |          +-- triggerRandom() -> void
  |          +-- setVolume(uint8_t volume) -> void
  |          +-- getVolume() -> uint8_t
  |          +-- isPlaying() -> bool
  |          +-- stop() -> void
  |          |
  |          +------- MP3TriggerDriver
  |          |          Wraps SparkFun MP3Trigger library.
  |          |          Calls MP3Trigger::update() in update() cycle.
  |          |
  |          +------- MockAudioDriver
  |                     Records trigger calls with timestamps.
  |                     No hardware access. Always reports kReady.
  |
  +------- ISensorDriver (abstract)
  |          Extends IDriver for sensor inputs.
  |          |
  |          +-- read() -> int32_t                     // Raw filtered value
  |          +-- readScaled() -> float                  // Normalized / calibrated value
  |          +-- hasChanged() -> bool                   // Value changed since last read
  |          +-- setCalibration(int32_t min, int32_t max) -> void
  |          |
  |          +------- AnalogSensorDriver
  |          |          Wraps AnalogMonitor with noise filtering.
  |          |          Performs analogRead() + exponential smoothing in update().
  |          |          Configurable via NVS for pin, resolution, snap multiplier.
  |          |
  |          +------- MockSensorDriver
  |                     Allows programmatic injection of sensor values.
  |                     injectValue(int32_t) sets the next read() return.
  |
  +------- ICommDriver (abstract)
             Extends IDriver for inter-board serial communication.
             |
             +-- send(DataType type, const char* data) -> void
             +-- receive() -> bool                     // Returns true if new message available
             +-- getLastMessage() -> const Message&
             +-- setCallback(MessageCallback cb) -> void
             |
             +------- PenumbraCommDriver
             |          Wraps MessageHandler with RingBuffer.
             |          Handles ACK/NACK/retry protocol.
             |
             +------- MockCommDriver
                        Stores sent messages in a queue.
                        Allows injecting received messages for testing.
```

### 2.1 DriverStatus Enum

```cpp
enum class DriverStatus : uint8_t {
    kUninitialized = 0,   // init() not yet called
    kReady,               // Operating normally
    kDegraded,            // Operating with warnings (e.g., high latency)
    kError,               // Non-recoverable error; needs reset()
    kDisabled,            // Explicitly disabled via shutdown()
};
```

### 2.2 ErrorInfo Structure

```cpp
struct ErrorInfo {
    uint16_t code;            // Driver-specific error code (0 = no error)
    uint64_t timestamp;       // When the error occurred (ms since boot)
    char message[64];         // Human-readable description
};
```

### 2.3 IDriver Base Interface

```cpp
class IDriver {
public:
    virtual ~IDriver() = default;

    virtual DriverStatus init() = 0;
    virtual void update() = 0;
    virtual DriverStatus getStatus() const = 0;
    virtual ErrorInfo getErrorState() const = 0;
    virtual const char* getName() const = 0;
    virtual DriverStatus reset() = 0;
    virtual void shutdown() = 0;

    // Diagnostic command interface
    virtual bool handleDiagnostic(const char* command, char* response, size_t maxLen) {
        // Default: no diagnostic commands supported
        (void)command; (void)response; (void)maxLen;
        return false;
    }
};
```

---

## 3. UART Allocation Strategy

### 3.1 Current Pin Allocation (Penumbra Board)

The ESP32-WROOM-32D has 3 hardware UARTs. The project uses SoftwareSerial (EspSoftwareSerial) extensively because:
- UART0 is reserved for USB console/Bluepad32 debugging
- UART2 is used for OpenMV (high-speed, 115200 baud)
- All other peripherals share 4 SoftwareSerial instances

```
UART Port      | Pins (RX/TX)   | Device             | Baud   | Mode
---------------|----------------|--------------------|--------|----------
HW UART0       | GPIO3/GPIO1    | USB Console/BP32   | 115200 | Reserved
HW UART2       | GPIO16/GPIO17  | OpenMV Camera      | 115200 | Bidi
SW UART-A      | N/A/GPIO16     | Sabertooth bus     | 9600   | TX-only
SW UART-B      | GPIO32/GPIO4   | Maestro Body       | 9600   | Bidi
SW UART-C      | GPIO13/GPIO14  | Maestro Dome       | 9600   | Bidi
SW UART-D      | GPIO22/GPIO21  | MP3 Trigger        | 38400  | Bidi
```

**Critical observation**: GPIO16 is shared between HW UART2 RX (OpenMV) and the Sabertooth TX line. The current PinMap.h defines `PIN_SABERTOOTH_TX` as `PIN_SERIAL3_RX` (GPIO16). This is valid because Sabertooth is TX-only from the ESP32 side and uses SoftwareSerial, but it creates a pin conflict if OpenMV and Sabertooth are both active. This must be resolved in the HAL by enforcing mutual exclusion or remapping.

### 3.2 UART Bus Manager

Rather than letting each driver independently configure UART, the HAL introduces a `UartBusManager` that owns all UART port lifecycle:

```cpp
struct UartPortConfig {
    uint8_t portId;              // Logical port ID (0-5)
    gpio_num_t rxPin;            // GPIO_NUM_NC if unused
    gpio_num_t txPin;            // GPIO_NUM_NC if unused
    uint32_t baudRate;
    bool isSoftwareSerial;       // true = EspSoftwareSerial, false = HardwareSerial
    bool isHalfDuplex;           // true = TX-only or RX-only
    const char* ownerName;       // Driver name for diagnostics
};

class UartBusManager {
public:
    // Register a UART port configuration. Returns nullptr on pin conflict.
    Stream* acquirePort(const UartPortConfig& config);

    // Release a port for reassignment.
    void releasePort(uint8_t portId);

    // Check for pin conflicts across all registered ports.
    bool validateAllocations() const;

    // Get diagnostic info for all ports.
    void printAllocations() const;

private:
    static constexpr uint8_t kMaxPorts = 6;
    UartPortConfig m_ports[kMaxPorts];
    Stream* m_streams[kMaxPorts];
    bool m_allocated[kMaxPorts];

    // Pin conflict detection: tracks which GPIO is claimed by which port.
    uint8_t m_pinOwner[GPIO_NUM_MAX];  // Maps GPIO -> portId (0xFF = unclaimed)
};
```

### 3.3 Shared-Bus Protocol (Sabertooth)

The Sabertooth and SyRen share a single UART TX line using Packet Serial addressing. The HAL models this as:

```
UartBusManager owns one SoftwareSerial instance for the Sabertooth bus.
  |
  +-- SabertoothBusDriver (IDriver) manages the shared bus
        |
        +-- SabertoothMotorDriver(addr=129, motor=1)  // Left foot
        +-- SabertoothMotorDriver(addr=129, motor=2)  // Right foot
        +-- SabertoothMotorDriver(addr=128, motor=1)  // Dome spin
```

The `SabertoothBusDriver` handles:
- Autobaud on init
- Serial timeout configuration
- Scheduling TX writes to avoid collisions (round-robin in update())
- Error detection (timeout on expected responses, though Sabertooth is TX-only)

### 3.4 Future: DMA-Based UART

For high-throughput bidirectional communication (OpenMV at 115200, potential ROS2 bridge), the HAL reserves the HW UART2 path for DMA:

```cpp
class DmaUartDriver : public IDriver {
public:
    DmaUartDriver(uart_port_t port, gpio_num_t tx, gpio_num_t rx, uint32_t baud);

    DriverStatus init() override;
    void update() override;

    // DMA-specific: non-blocking read/write with ring buffers
    size_t write(const uint8_t* data, size_t len);
    size_t read(uint8_t* buffer, size_t maxLen);
    size_t available() const;

private:
    uart_port_t m_port;
    // ESP-IDF UART driver with DMA handles allocation internally
    // Ring buffer sizes configured at init: TX=512, RX=1024 bytes
    static constexpr size_t kTxBufSize = 512;
    static constexpr size_t kRxBufSize = 1024;
};
```

This uses `uart_driver_install()` from ESP-IDF with DMA-backed ring buffers, providing interrupt-driven non-blocking I/O. The DmaUartDriver is an IDriver, so it participates in the same lifecycle management.

---

## 4. Driver Lifecycle Management

### 4.1 Lifecycle State Machine

```
  [Uninitialized] --init()--> [Ready] --shutdown()--> [Disabled]
         |                      |   ^                      |
         |                      |   |--reset()--           |
         |                      v                  |       |
         |                   [Degraded] ---reset()-+       |
         |                      |                          |
         |                      v                          |
         +--init() fail--> [Error] ----reset()--->[Ready]  |
                              ^                            |
                              |------- init() after -------+
                                        re-enable
```

### 4.2 DriverManager

The `DriverManager` is a singleton that owns all IDriver instances and orchestrates their lifecycle:

```cpp
class DriverManager {
public:
    static constexpr uint8_t kMaxDrivers = 12;

    // Registration (called during setup, before any init)
    bool registerDriver(IDriver* driver, uint8_t priority = 128);

    // Lifecycle orchestration
    void initAll();       // Calls init() on all registered drivers in priority order
    void updateAll();     // Calls update() on all drivers with status kReady or kDegraded
    void shutdownAll();   // Calls shutdown() on all drivers in reverse priority order
    void resetDriver(const char* name);  // Reset a specific driver by name

    // Health monitoring
    void checkHealth();   // Called periodically; transitions drivers to kDegraded/kError
    DriverStatus getDriverStatus(const char* name) const;
    void printDiagnostics() const;

    // Diagnostic command routing
    bool routeDiagnostic(const char* driverName, const char* command,
                         char* response, size_t maxLen);

private:
    struct DriverEntry {
        IDriver* driver;
        uint8_t priority;      // Lower = higher priority (initialized first)
        uint64_t lastUpdateUs;  // For timing budget tracking
        uint64_t worstCaseUs;   // Worst observed update() duration
    };

    DriverEntry m_drivers[kMaxDrivers];
    uint8_t m_driverCount = 0;
};
```

### 4.3 Initialization Order

Drivers are initialized in priority order to respect hardware dependencies:

| Priority | Driver                    | Reason                                         |
|----------|---------------------------|-------------------------------------------------|
| 10       | UartBusManager            | Must configure buses before any UART driver      |
| 20       | SabertoothBusDriver       | Autobaud must complete before motor commands     |
| 30       | SabertoothMotorDriver x3  | Depends on bus being ready                       |
| 40       | MaestroServoDriver (body) | UART must be configured                          |
| 41       | MaestroServoDriver (dome) | UART must be configured                          |
| 50       | MP3TriggerDriver          | Independent UART, but lower priority than motors |
| 60       | AnalogSensorDriver        | ADC, no bus dependency                           |
| 70       | DmaUartDriver (OpenMV)    | Independent HW UART                              |
| 80       | PenumbraCommDriver        | Depends on UART allocation                       |
| 90       | GpioDriver (LEDs)         | Lowest priority, pure GPIO                       |

### 4.4 Update Cycle Integration with Executor

The DriverManager integrates with the core Executor's tick loop:

```
Executor tick (10ms period):
  1. BP32.update()              // Bluetooth controller polling
  2. DriverManager::updateAll() // All hardware drivers
  3. Node processing            // Message broker, subscriptions
  4. MotorSafety::CheckMotors() // Watchdog enforcement
  5. vTaskDelay remainder        // Yield to RTOS
```

Each driver's `update()` must complete within its timing budget (see Section 8). The DriverManager measures each `update()` call duration and flags drivers that exceed their budget.

---

## 5. NVS Configuration Schema

### 5.1 Namespace Layout

NVS keys are organized by driver namespace. Each driver has a dedicated NVS namespace to avoid key collisions:

```
Namespace: "hal_uart"
  Key: "saber_baud"    Type: u32    Default: 9600
  Key: "saber_tx_pin"  Type: u8     Default: 16
  Key: "maestb_baud"   Type: u32    Default: 9600
  Key: "maestb_rx"     Type: u8     Default: 32
  Key: "maestb_tx"     Type: u8     Default: 4
  Key: "maestd_baud"   Type: u32    Default: 9600
  Key: "maestd_rx"     Type: u8     Default: 13
  Key: "maestd_tx"     Type: u8     Default: 14
  Key: "mp3_baud"      Type: u32    Default: 38400
  Key: "mp3_rx"        Type: u8     Default: 22
  Key: "mp3_tx"        Type: u8     Default: 21
  Key: "omv_baud"      Type: u32    Default: 115200
  Key: "omv_rx"        Type: u8     Default: 33
  Key: "omv_tx"        Type: u8     Default: 25

Namespace: "hal_motor"
  Key: "saber_addr"    Type: u8     Default: 129
  Key: "syren_addr"    Type: u8     Default: 128
  Key: "drive_maxspd"  Type: f32    Default: 0.25
  Key: "drive_boost"   Type: f32    Default: 0.15
  Key: "drive_ramp"    Type: u8     Default: 80
  Key: "drive_dead"    Type: f32    Default: 0.05
  Key: "drive_m1_inv"  Type: u8     Default: 1 (true)
  Key: "drive_m2_inv"  Type: u8     Default: 0 (false)
  Key: "dome_maxspd"   Type: f32    Default: 0.80
  Key: "dome_ramp"     Type: u8     Default: 80
  Key: "dome_dead"     Type: f32    Default: 0.05
  Key: "dome_m1_inv"   Type: u8     Default: 0 (false)
  Key: "dome_slew"     Type: f32    Default: 2.0
  Key: "safety_en"     Type: u8     Default: 1 (true)
  Key: "safety_ms"     Type: u16    Default: 500

Namespace: "hal_servo"
  Key: "body_id"       Type: u8     Default: 12
  Key: "dome_id"       Type: u8     Default: 13
  Key: "body_chans"    Type: u8     Default: 6
  Key: "dome_chans"    Type: u8     Default: 11
  // Per-channel calibration stored as blobs:
  Key: "body_cal"      Type: blob   Default: {min, max, neutral, speed, accel} x 6 channels
  Key: "dome_cal"      Type: blob   Default: {min, max, neutral, speed, accel} x 11 channels

Namespace: "hal_rss"
  Key: "base_alt"      Type: f32    Default: 149.053
  Key: "ee_alt"        Type: f32    Default: 193.350
  Key: "bot_link"      Type: f32    Default: 45.0
  Key: "top_link"      Type: f32    Default: 31.0
  Key: "min_height"    Type: f32    Default: 28.621
  Key: "lim_nv"        Type: f32    Default: 0.25
  Key: "bend_out"      Type: u8     Default: 1
  Key: "act_range"     Type: u16    Default: 270
  Key: "rot_offset"    Type: f32    Default: -30.0

Namespace: "hal_audio"
  Key: "volume"        Type: u8     Default: 0

Namespace: "hal_sensor"
  Key: "dome_pot_pin"  Type: u8     Default: 34
  Key: "dome_pot_min"  Type: u16    Default: 1225
  Key: "dome_pot_max"  Type: u16    Default: 2500
  Key: "dome_snap"     Type: f32    Default: 0.01
```

### 5.2 NVS Configuration Loader

```cpp
class NvsConfig {
public:
    // Open a namespace and load all keys with defaults.
    static bool load(const char* ns, const char* key, uint32_t* out, uint32_t defaultVal);
    static bool load(const char* ns, const char* key, float* out, float defaultVal);
    static bool load(const char* ns, const char* key, uint8_t* out, uint8_t defaultVal);
    static bool load(const char* ns, const char* key, uint16_t* out, uint16_t defaultVal);
    static bool loadBlob(const char* ns, const char* key, void* out, size_t* len);

    // Save updated values (for web UI / diagnostic commands).
    static bool save(const char* ns, const char* key, uint32_t val);
    static bool save(const char* ns, const char* key, float val);
    static bool save(const char* ns, const char* key, uint8_t val);
    static bool saveBlob(const char* ns, const char* key, const void* data, size_t len);

    // Reset a namespace to defaults (erase all keys).
    static bool resetNamespace(const char* ns);
};
```

Each driver's `init()` calls `NvsConfig::load()` for its parameters before configuring hardware. If NVS has no stored value, the compile-time default from SettingsUser.h / SettingsSystem.h is used and written to NVS for future persistence.

---

## 6. Mock Driver Architecture

### 6.1 Purpose

Mock drivers serve three use cases:
1. **Desktop unit testing**: Compile the entire node graph and controller logic on a host PC, replacing all hardware drivers with mocks.
2. **ESP32 simulation mode**: Run on real hardware but with mock drivers to test control flow without connected peripherals.
3. **Partial mock**: Mix real and mock drivers (e.g., real Sabertooth + mock Maestro) for incremental hardware bring-up.

### 6.2 MockDriverBase

```cpp
class MockDriverBase : public IDriver {
public:
    MockDriverBase(const char* name) : m_name(name) {}

    DriverStatus init() override {
        m_status = DriverStatus::kReady;
        m_initCount++;
        return m_status;
    }

    void update() override { m_updateCount++; }

    DriverStatus getStatus() const override { return m_status; }

    ErrorInfo getErrorState() const override { return m_lastError; }

    const char* getName() const override { return m_name; }

    DriverStatus reset() override {
        m_status = DriverStatus::kReady;
        m_resetCount++;
        return m_status;
    }

    void shutdown() override {
        m_status = DriverStatus::kDisabled;
    }

    // Test instrumentation
    void injectError(uint16_t code, const char* msg);
    void setStatus(DriverStatus status) { m_status = status; }
    uint32_t getInitCount() const { return m_initCount; }
    uint32_t getUpdateCount() const { return m_updateCount; }
    uint32_t getResetCount() const { return m_resetCount; }

protected:
    const char* m_name;
    DriverStatus m_status = DriverStatus::kUninitialized;
    ErrorInfo m_lastError = {};
    uint32_t m_initCount = 0;
    uint32_t m_updateCount = 0;
    uint32_t m_resetCount = 0;
};
```

### 6.3 Mock Motor Driver

```cpp
class MockMotorDriver : public MockDriverBase, public IMotorDriver {
public:
    MockMotorDriver(const char* name) : MockDriverBase(name) {}

    // IMotorDriver interface
    void set(float speed) override {
        m_lastSpeed = speed;
        m_setHistory[m_historyIdx++ % kHistorySize] = speed;
    }
    float get() const override { return m_lastSpeed; }
    void setInverted(bool inv) override { m_inverted = inv; }
    bool isInverted() const override { return m_inverted; }
    void disable() override { m_lastSpeed = 0.0f; m_disabled = true; }
    void stop() override { m_lastSpeed = 0.0f; }

    // Test inspection
    float getSpeedAt(uint32_t index) const { return m_setHistory[index % kHistorySize]; }
    uint32_t getSetCount() const { return m_historyIdx; }

private:
    static constexpr uint32_t kHistorySize = 64;
    float m_setHistory[kHistorySize] = {};
    uint32_t m_historyIdx = 0;
    float m_lastSpeed = 0.0f;
    bool m_inverted = false;
    bool m_disabled = false;
};
```

### 6.4 Build-Time Driver Selection

Mock vs. real drivers are selected via a build flag:

```cmake
# In CMakeLists.txt
option(CHOPPER_USE_MOCK_HAL "Use mock drivers instead of real hardware" OFF)

if(CHOPPER_USE_MOCK_HAL)
    target_compile_definitions(${COMPONENT_LIB} PUBLIC CHOPPER_MOCK_HAL=1)
endif()
```

Driver factory uses the flag:

```cpp
IMotorDriver* createFootMotorDriver(uint8_t motorId) {
#if CHOPPER_MOCK_HAL
    return new MockMotorDriver("foot_motor");
#else
    return new SabertoothMotorDriver(sabertoothBus, SABERTOOTH_TANK_DRIVE_ID, motorId);
#endif
}
```

---

## 7. Diagnostic Commands

Each driver implements `handleDiagnostic()` for runtime inspection and calibration. The diagnostic system is accessible via the serial console or a future web UI.

### 7.1 Command Format

```
diag <driver_name> <command> [args...]
```

### 7.2 Per-Driver Commands

**SabertoothMotorDriver:**
```
diag foot_left status          -> "kReady, speed=0.15, inverted=true"
diag foot_left set 0.5         -> Sets motor to 50% (temporary, for testing)
diag foot_left stop            -> Emergency stop
diag foot_left calibrate       -> Reports current NVS config
```

**MaestroServoDriver:**
```
diag maestro_body status       -> "kReady, 6 channels, errors=0"
diag maestro_body pos 0        -> "channel 0: pulse=2256us, enabled=true"
diag maestro_body set 0 1500   -> Move channel 0 to 1500us (temporary)
diag maestro_body home         -> Move all channels to neutral positions
diag maestro_body disable      -> Disable all PWM outputs
```

**MP3TriggerDriver:**
```
diag mp3 status                -> "kReady, volume=0, playing=false"
diag mp3 play 3                -> Trigger track 3
diag mp3 volume 20             -> Set volume to 20
```

**AnalogSensorDriver:**
```
diag dome_pot status           -> "kReady, raw=1850, angle=147deg"
diag dome_pot calibrate min    -> Store current raw value as min
diag dome_pot calibrate max    -> Store current raw value as max
```

### 7.3 System-Wide Diagnostics

```
diag list                      -> List all registered drivers with status
diag health                    -> Full health report (status, errors, timing)
diag uart                      -> UART bus allocation table
diag nvs <namespace>           -> Dump all NVS keys in namespace
diag reset <driver_name>       -> Reset a specific driver
```

---

## 8. Memory and Timing Budgets

### 8.1 Memory Budget per Driver

Total ESP32-WROOM-32D SRAM: ~320 KB (usable ~160 KB after FreeRTOS, WiFi/BT stack, and framework overhead).

Target: HAL drivers should consume no more than **20 KB** total.

| Driver                        | Static RAM | Stack (update) | Heap (init) | Total Est. |
|-------------------------------|------------|----------------|-------------|------------|
| UartBusManager                | 256 B      | 64 B           | 0 B         | 320 B      |
| SabertoothBusDriver           | 128 B      | 32 B           | 0 B         | 160 B      |
| SabertoothMotorDriver (x3)    | 48 B each  | 16 B           | 0 B         | 192 B      |
| MaestroServoDriver (body, 6ch)| 512 B      | 128 B          | 0 B         | 640 B      |
| MaestroServoDriver (dome,11ch)| 896 B      | 192 B          | 0 B         | 1088 B     |
| ServoState (x17 channels)     | 68 B each  | 16 B           | 0 B         | 1,156 B    |
| MP3TriggerDriver              | 128 B      | 32 B           | 256 B (lib) | 416 B      |
| AnalogSensorDriver            | 96 B       | 16 B           | 0 B         | 112 B      |
| DmaUartDriver (OpenMV)        | 64 B       | 32 B           | 1,536 B (bufs)| 1,632 B  |
| PenumbraCommDriver            | 7,424 B    | 128 B          | 0 B         | 7,552 B    |
| GpioDriver (LEDs)             | 16 B       | 8 B            | 0 B         | 24 B       |
| DriverManager                 | 256 B      | 64 B           | 0 B         | 320 B      |
| NvsConfig (static methods)    | 0 B        | 256 B          | 0 B         | 256 B      |
| **TOTAL**                     |            |                |             | **~13.9 KB**|

Notes:
- PenumbraCommDriver is the largest consumer due to RingBuffer arrays (`Message` struct at ~260 bytes x 25 slots x 2 buffers = 13 KB). Consider reducing BUFFER_SIZE or BUFFER_DATA_MAX_SIZE.
- Servo state arrays are pre-allocated in the MaestroServoDriver, not dynamically via `std::vector` as in the current code. This eliminates heap fragmentation.
- SoftwareSerial instances (4 x ~64 bytes each) are owned by UartBusManager.

### 8.2 Timing Budget per Update Cycle

Target main loop period: **10 ms** (100 Hz), matching current `vTaskDelay(pdMS_TO_TICKS(10))`.

Timing budget for `DriverManager::updateAll()`: **4 ms maximum** (leaving 6 ms for controller polling, node processing, and RTOS overhead).

| Driver                        | Budget   | Expected | Notes                                     |
|-------------------------------|----------|----------|-------------------------------------------|
| SabertoothMotorDriver (x3)    | 300 us   | 150 us   | 3x packet serial TX at 9600 baud          |
| MaestroServoDriver (body)     | 600 us   | 400 us   | setMultiTarget for 6 channels             |
| MaestroServoDriver (dome)     | 800 us   | 500 us   | setMultiTarget for 11 channels            |
| MP3TriggerDriver              | 200 us   | 50 us    | MP3Trigger::update() polling              |
| AnalogSensorDriver            | 200 us   | 100 us   | analogRead() + filtering                  |
| DmaUartDriver                 | 100 us   | 20 us    | DMA buffer check only (non-blocking)      |
| PenumbraCommDriver            | 500 us   | 200 us   | Check incoming + retry logic              |
| GpioDriver                    | 50 us    | 10 us    | analogWrite() for LED                     |
| DriverManager overhead        | 250 us   | 100 us   | Iteration, timing measurement             |
| **TOTAL**                     | **3 ms** | **1.5 ms**|                                           |

### 8.3 Timing Enforcement

The DriverManager tracks per-driver update duration:

```cpp
void DriverManager::updateAll() {
    for (uint8_t i = 0; i < m_driverCount; i++) {
        if (m_drivers[i].driver->getStatus() == DriverStatus::kReady ||
            m_drivers[i].driver->getStatus() == DriverStatus::kDegraded) {

            uint64_t start = esp_timer_get_time();  // microseconds
            m_drivers[i].driver->update();
            uint64_t elapsed = esp_timer_get_time() - start;

            m_drivers[i].lastUpdateUs = elapsed;
            if (elapsed > m_drivers[i].worstCaseUs) {
                m_drivers[i].worstCaseUs = elapsed;
            }
        }
    }
}
```

If a driver consistently exceeds its budget, it is transitioned to `kDegraded`. If it exceeds 2x its budget, it is transitioned to `kError` and skipped on subsequent cycles until `reset()` is called.

---

## 9. Power Management

### 9.1 Driver Power States

Each driver supports graceful shutdown:

```
Active  --(shutdown())--> Disabled  --(init())--> Active
Active  --(sleep())----> LowPower  --(wake())--> Active
```

Power management is optional -- drivers that do not support sleep simply ignore `sleep()`/`wake()` calls.

### 9.2 Sleep-Capable Drivers

| Driver              | Sleep Action                              | Wake Action                 |
|---------------------|-------------------------------------------|-----------------------------|
| SabertoothMotorDriver| Send stop command, no further TX          | Resume normal operation      |
| MaestroServoDriver   | Disable all PWM outputs (target=0)        | Re-enable, move to neutral   |
| MP3TriggerDriver     | Stop playback, mute                       | Restore volume               |
| DmaUartDriver        | Disable UART peripheral clock             | Re-init UART                 |
| GpioDriver (LEDs)    | Set outputs LOW                           | Restore previous state       |

### 9.3 System Power Modes

```cpp
enum class PowerMode : uint8_t {
    kActive,      // All drivers running
    kIdle,        // No controller input for >30s; non-essential drivers sleep
    kStandby,     // Only safety-critical drivers active (motor watchdog)
    kShutdown,    // All drivers disabled, minimum power
};
```

The Executor monitors controller activity and transitions power modes:
- **Active -> Idle**: No controller input for 30 seconds. Sleep audio and servo drivers.
- **Idle -> Active**: Any controller input received. Wake all drivers.
- **Idle -> Standby**: No controller input for 5 minutes. Sleep all non-safety drivers.
- **Any -> Shutdown**: Explicit command or critical battery voltage.

---

## 10. Migration Path from Current Code

### 10.1 Phase 1: Interface Extraction (Non-Breaking)

1. Define `IDriver`, `IMotorDriver`, `IServoController`, `IAudioDriver`, `ISensorDriver` as abstract interfaces in `main/include/chopper/hal/`.
2. Current concrete classes (`SabertoothController`, `ServoDispatch`, `ExtendedMP3Trigger`, `AnalogMonitor`) remain unchanged.
3. Write adapter classes that wrap existing implementations behind the new interfaces.

### 10.2 Phase 2: DriverManager Integration

1. Implement `DriverManager` and register adapted drivers.
2. Move initialization from `setup()` functions in `sketch.cpp` into `DriverManager::initAll()`.
3. Move per-frame updates into `DriverManager::updateAll()`.
4. The `Controllers` class receives `IMotorDriver*`, `IServoController*`, etc. instead of concrete types.

### 10.3 Phase 3: NVS and Configuration

1. Implement `NvsConfig` loader.
2. Replace `#define` constants in `SettingsUser.h` / `SettingsSystem.h` with NVS-backed values that fall back to the compile-time defaults.
3. Pin assignments in `PinMap.h` become NVS-overridable via `UartBusManager`.

### 10.4 Phase 4: Mock Drivers and Testing

1. Implement mock drivers for each interface.
2. Add CMake build target for host-PC compilation with mock HAL.
3. Write unit tests that exercise the `Controllers` logic with mock drivers.

### 10.5 Phase 5: Advanced Features

1. DMA UART driver for OpenMV.
2. Power management integration.
3. Diagnostic command parser and web UI integration.

---

## 11. Open Questions and Risks

1. **GPIO16 conflict**: Sabertooth TX and OpenMV RX both claim GPIO16. If both are active simultaneously, data corruption will occur. Resolution: remap Sabertooth TX to an available GPIO (GPIO26 via RS485 RTS pin is a candidate if RS485 is unused) or time-multiplex the pin.

2. **SoftwareSerial reliability at 38400**: The MP3 Trigger runs at 38400 baud on SoftwareSerial, which is near the practical limit for bit-banged UART on ESP32. If reliability issues arise, consider dedicating HW UART1 (by remapping pins from the flash-connected defaults) or reducing baud rate.

3. **Heap fragmentation from std::vector**: The current `ServoDispatch` uses `std::vector` for channel targets and servo states. The HAL design replaces these with fixed-size arrays sized at compile time. This requires knowing the maximum channel count at compile time, which is acceptable for this application.

4. **RingBuffer memory**: The `PenumbraCommDriver` (wrapping `MessageHandler`) consumes ~13 KB for its dual ring buffers. If memory pressure becomes an issue, reduce `BUFFER_SIZE` from 25 to 10 and `BUFFER_DATA_MAX_SIZE` from 256 to 128.

5. **Sabertooth shared bus timing**: Multiple Sabertooth/SyRen devices on one TX line require sequential writes. At 9600 baud, a typical command packet (4 bytes) takes ~4.2 ms to transmit. Three motor updates per cycle would take ~12.6 ms, exceeding the 10 ms loop budget. Mitigation: stagger motor updates across cycles (update foot motors on even ticks, dome motor on odd ticks) or increase baud rate to 19200/38400 if hardware supports it.

6. **NVS write endurance**: ESP32 NVS uses flash, which has limited write cycles (~100K). Configuration saves should be rate-limited (no more than once per minute) and only written when values actually change.
