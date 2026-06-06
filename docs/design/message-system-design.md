# Message and Topic System Design

## 1. Overview

This document specifies the message passing, topic routing, and supporting subsystems (timers, services, parameter server) for the Chopper framework running on ESP32. The design targets deterministic latency, bounded memory, and zero-copy delivery for the common single-subscriber case, while remaining approachable for hobbyist programmers.

### Design Principles

1. **Zero-copy default** -- publishers and subscribers share a `const` pointer when QoS allows it.
2. **Static memory** -- every queue, pool, and registry is sized at compile time; no `malloc` in the hot path.
3. **Priority-aware** -- higher-priority topics preempt lower-priority ones inside the broker.
4. **Introspectable** -- every topic exposes counters and latency histograms readable from a debug shell or web UI.
5. **ESP32-first** -- designed for single-core FreeRTOS with optional SMP extensions; fits comfortably in 320 KB SRAM.

---

## 2. Current State Analysis

### Existing Code Strengths

| File | What it provides |
|------|-----------------|
| `Message.h` | `TypedMessage<T>` CRTP base with timestamp, type name, size, and clone |
| `Publisher.h` | Topic-based fan-out via weak_ptr subscription list |
| `Subscription.h` | Type-safe callback with `dynamic_cast` verification |
| `MessageBroker.h` | Singleton registry with topic-to-publisher/subscription maps |
| `PublishingNode.h` | Convenience layer: nodes create pubs/subs through the broker |
| `CommonMessages.h` | Concrete messages: ControllerInput, MotorCommand, ServoCommand, etc. |
| `RingBuffer.h` | Fixed-size ring with `unordered_map` lookup for serial messaging |

### Identified Gaps

| Gap | Impact |
|-----|--------|
| `std::make_shared<MessageT>(message)` on every publish | Heap allocation on every message, fragmentation risk |
| `std::unordered_map` for topic registry | Heap allocation, non-deterministic rehash |
| `std::string` for topic names | Heap allocation per publisher/subscription |
| `std::mutex` in statistics path | Priority inversion risk under FreeRTOS |
| No message priority | Safety-critical commands queued behind sensor data |
| No timer/periodic callback | Nodes must self-schedule in `process()` |
| No service (request/response) pattern | Controller configuration requires ad-hoc messaging |
| No parameter server | Runtime tuning requires recompilation |
| `dynamic_cast` on every message delivery | RTTI overhead; fails if `-fno-rtti` is used |

---

## 3. Architecture

### 3.1 Layered Architecture Diagram

```
+-------------------------------------------------------------------+
|                        Application Nodes                          |
|   DriveNode    DomeNode    AudioNode    SensorNode    LEDNode     |
+-------------------------------------------------------------------+
        |  publish()   |  subscribe()   |  call()   |  getParam()
        v              v                v           v
+-------------------------------------------------------------------+
|                    PublishingNode (convenience API)                |
+-------------------------------------------------------------------+
        |              |                |           |
        v              v                v           v
+--------+  +-----------+  +-----------+  +------------------+
| Topics |  | Services  |  |  Timers   |  | Parameter Server |
+--------+  +-----------+  +-----------+  +------------------+
        \         |              |            /
         \        |              |           /
          v       v              v          v
+-------------------------------------------------------------------+
|                        MessageBroker                              |
|  TopicRegistry  |  MessagePool  |  PriorityDispatcher  |  Stats  |
+-------------------------------------------------------------------+
        |
        v
+-------------------------------------------------------------------+
|                    Executor (scheduling layer)                    |
+-------------------------------------------------------------------+
```

### 3.2 Data Flow

```
Publisher::publish(msg)
    |
    +---> MessagePool::acquire() or zero-copy shared_ptr
    |         (static pool for small msgs, shared_ptr for large)
    |
    +---> TopicRegistry::getSubscriptions(topic_id)
    |
    +---> for each subscription:
    |       if (priority >= current_threshold)
    |           subscription->enqueue(msg_ptr)   // lock-free SPSC ring
    |       else
    |           PriorityDispatcher::defer(sub, msg_ptr)
    |
    +---> Statistics::recordPublish(topic_id, latency)
```

---

## 4. Topic System

### 4.1 Topic Identifiers

Replace `std::string` topic names with compile-time hashed topic IDs to avoid heap allocation.

```cpp
// Compile-time FNV-1a hash for topic names
constexpr uint32_t fnv1a(const char* str, uint32_t hash = 2166136261u) {
    return (*str == 0) ? hash :
        fnv1a(str + 1, (hash ^ static_cast<uint32_t>(*str)) * 16777619u);
}

// Usage: constexpr TopicId TOPIC_DRIVE_CMD = fnv1a("drive/cmd");
using TopicId = uint32_t;

// Macro for convenience (stores string for debug, hash for routing)
#define CHOPPER_TOPIC(name) \
    constexpr ::chopper::core::TopicId TOPIC_##name = ::chopper::core::fnv1a(#name)

// Predefined topics
namespace chopper::topics {
    CHOPPER_TOPIC(controller_input);   // controller/input
    CHOPPER_TOPIC(drive_cmd);          // drive/cmd
    CHOPPER_TOPIC(servo_cmd);          // servo/cmd
    CHOPPER_TOPIC(sensor_data);        // sensor/data
    CHOPPER_TOPIC(system_status);      // system/status
    CHOPPER_TOPIC(audio_cmd);          // audio/cmd
    CHOPPER_TOPIC(led_cmd);            // led/cmd
    CHOPPER_TOPIC(safety_estop);       // safety/estop
}
```

### 4.2 Topic Registry

Statically allocated, fixed-capacity registry.

```cpp
static constexpr size_t MAX_TOPICS = 32;
static constexpr size_t MAX_SUBSCRIBERS_PER_TOPIC = 10;

struct TopicEntry {
    TopicId         id;
    const char*     name;           // debug-only, stored in flash
    uint8_t         priority;       // 0 = lowest, 255 = highest
    QoSProfile      default_qos;
    uint8_t         subscriber_count;
    SubscriptionSlot subscribers[MAX_SUBSCRIBERS_PER_TOPIC];
    // Introspection
    uint32_t        publish_count;
    uint32_t        drop_count;
    uint32_t        max_latency_us;
    uint32_t        avg_latency_us; // exponential moving average
    bool            active;
};

class TopicRegistry {
public:
    TopicEntry* registerTopic(TopicId id, const char* name, uint8_t priority,
                              const QoSProfile& qos);
    TopicEntry* find(TopicId id);            // O(1) via hash table
    void forEachTopic(void(*visitor)(const TopicEntry&));  // introspection
private:
    TopicEntry topics_[MAX_TOPICS];
    uint8_t    count_ = 0;
};
```

### 4.3 Priority Levels

| Priority | Value | Use Case | Deadline |
|----------|-------|----------|----------|
| CRITICAL | 255 | Emergency stop, safety | < 100 us |
| HIGH | 192 | Motor/servo commands | < 500 us |
| NORMAL | 128 | Controller input, audio | < 1 ms |
| LOW | 64 | LED patterns, telemetry | < 10 ms |
| BACKGROUND | 0 | Diagnostics, logging | Best effort |

---

## 5. Message Pool (Static Allocation)

### 5.1 Pool Design

A typed, fixed-size object pool that avoids heap allocation for messages.

```cpp
template<typename T, size_t N>
class MessagePool {
    static_assert(std::is_trivially_copyable_v<T> || std::is_base_of_v<Message, T>,
                  "Pool type must be trivially copyable or a Message subclass");
public:
    struct Slot {
        alignas(T) uint8_t storage[sizeof(T)];
        std::atomic<bool>   in_use{false};
        uint16_t            generation;    // ABA protection
    };

    /// Acquire a slot, returns nullptr if pool exhausted
    T* acquire() {
        for (size_t i = 0; i < N; ++i) {
            bool expected = false;
            if (slots_[i].in_use.compare_exchange_strong(expected, true,
                    std::memory_order_acquire)) {
                slots_[i].generation++;
                return new (slots_[i].storage) T();
            }
        }
        return nullptr;  // pool exhausted
    }

    /// Release a slot back to the pool
    void release(T* ptr) {
        for (size_t i = 0; i < N; ++i) {
            if (reinterpret_cast<T*>(slots_[i].storage) == ptr) {
                ptr->~T();
                slots_[i].in_use.store(false, std::memory_order_release);
                return;
            }
        }
    }

    /// Pool statistics
    size_t available() const {
        size_t count = 0;
        for (size_t i = 0; i < N; ++i)
            if (!slots_[i].in_use.load(std::memory_order_relaxed)) ++count;
        return count;
    }

    size_t capacity() const { return N; }

    /// High-water mark (max simultaneous usage)
    size_t highWaterMark() const { return high_water_mark_; }

private:
    Slot    slots_[N];
    size_t  high_water_mark_ = 0;
};
```

### 5.2 Per-Type Pool Sizing

| Message Type | `sizeof` (est.) | Pool Size | Total RAM |
|-------------|-----------------|-----------|-----------|
| ControllerInput | 128 bytes | 4 | 512 B |
| MotorCommand | 12 bytes | 16 | 192 B |
| ServoCommand | 12 bytes | 16 | 192 B |
| SensorData | 12 bytes | 16 | 192 B |
| SystemStatus | 72 bytes | 4 | 288 B |
| AudioCommand | 8 bytes | 8 | 64 B |
| LEDCommand | 12 bytes | 8 | 96 B |
| **Total** | | | **~1.5 KB** |

### 5.3 Zero-Copy Delivery

For the common case (single subscriber or BEST_EFFORT QoS), the published message pointer is delivered directly without copying. The reference-counting wrapper uses the pool slot's generation counter rather than `std::shared_ptr`:

```cpp
template<typename T>
class PooledRef {
public:
    PooledRef(T* ptr, MessagePool<T, N>* pool, uint16_t gen)
        : ptr_(ptr), pool_(pool), generation_(gen), ref_count_(1) {}

    void addRef() { ref_count_.fetch_add(1, std::memory_order_relaxed); }

    void release() {
        if (ref_count_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            pool_->release(ptr_);
        }
    }

    const T* get() const { return ptr_; }

private:
    T*                    ptr_;
    MessagePool<T, N>*    pool_;
    uint16_t              generation_;
    std::atomic<uint16_t> ref_count_;
};
```

---

## 6. Subscription Queues

### 6.1 Lock-Free SPSC Ring Buffer

Each subscription gets a fixed-size Single-Producer Single-Consumer ring buffer.

```cpp
template<typename T, size_t N>
class SPSCRing {
    static_assert((N & (N - 1)) == 0, "N must be power of 2");
public:
    bool push(const T& item) {
        size_t head = head_.load(std::memory_order_relaxed);
        size_t next = (head + 1) & (N - 1);
        if (next == tail_.load(std::memory_order_acquire)) {
            return false; // full
        }
        buffer_[head] = item;
        head_.store(next, std::memory_order_release);
        return true;
    }

    bool pop(T& item) {
        size_t tail = tail_.load(std::memory_order_relaxed);
        if (tail == head_.load(std::memory_order_acquire)) {
            return false; // empty
        }
        item = buffer_[tail];
        tail_.store((tail + 1) & (N - 1), std::memory_order_release);
        return true;
    }

    size_t size() const {
        size_t h = head_.load(std::memory_order_relaxed);
        size_t t = tail_.load(std::memory_order_relaxed);
        return (h - t) & (N - 1);
    }

    bool empty() const { return size() == 0; }

private:
    T buffer_[N];
    alignas(64) std::atomic<size_t> head_{0};
    alignas(64) std::atomic<size_t> tail_{0};
};
```

### 6.2 Queue Depth by QoS Profile

| QoS Profile | Queue Depth | Overflow Policy |
|-------------|-------------|-----------------|
| BEST_EFFORT, depth=1 | 2 (power of 2) | Overwrite oldest |
| RELIABLE, depth=5 | 8 (power of 2) | Block or drop with warning |
| commandAndControl | 8 | Drop oldest with log entry |
| sensorData | 2 | Overwrite (latest-only) |

---

## 7. Priority-Based Message Delivery

### 7.1 Dispatcher Design

The executor processes topics in priority order. Within a priority level, topics are serviced round-robin.

```cpp
class PriorityDispatcher {
public:
    static constexpr size_t NUM_PRIORITY_LEVELS = 4;

    /// Called by Executor each loop iteration
    void dispatch(uint64_t budget_us) {
        uint64_t start = esp_timer_get_time();
        // Process highest priority first
        for (int p = NUM_PRIORITY_LEVELS - 1; p >= 0; --p) {
            for (auto& topic : priority_buckets_[p]) {
                if ((esp_timer_get_time() - start) >= budget_us) return;
                deliverTopic(topic);
            }
        }
    }

private:
    void deliverTopic(TopicEntry& topic);

    // Topics grouped by priority bucket
    // Bucket 0=LOW/BG, 1=NORMAL, 2=HIGH, 3=CRITICAL
    StaticVector<TopicEntry*, MAX_TOPICS> priority_buckets_[NUM_PRIORITY_LEVELS];
};
```

### 7.2 Latency Tracking

Every message carries a publish timestamp. On delivery, latency is computed and recorded per-topic:

```cpp
struct LatencyStats {
    uint32_t min_us;
    uint32_t max_us;
    uint32_t avg_us;         // exponential moving average, alpha = 1/16
    uint32_t p99_us;         // approximated via histogram
    uint32_t sample_count;

    // 8-bucket histogram: [0-10us, 10-50, 50-100, 100-500, 500-1000, 1-5ms, 5-10ms, >10ms]
    uint32_t histogram[8];

    void record(uint32_t latency_us) {
        if (latency_us < min_us) min_us = latency_us;
        if (latency_us > max_us) max_us = latency_us;
        avg_us = avg_us - (avg_us >> 4) + (latency_us >> 4);  // EMA alpha=1/16
        ++sample_count;
        histogram[bucketIndex(latency_us)]++;
    }
};
```

---

## 8. Timer / Periodic Callback System

### 8.1 Design

Timers are lightweight structures managed by the executor. They do not create FreeRTOS software timers (which have heap overhead) but instead are polled each executor loop.

```cpp
using TimerCallback = void(*)(void* context);

struct TimerEntry {
    uint32_t       id;
    uint32_t       period_us;
    uint64_t       next_fire_us;
    TimerCallback  callback;
    void*          context;
    bool           one_shot;
    bool           active;
};

class TimerManager {
public:
    static constexpr size_t MAX_TIMERS = 16;

    /// Create a periodic timer. Returns timer ID.
    uint32_t createTimer(uint32_t period_us, TimerCallback cb, void* ctx,
                         bool one_shot = false);

    /// Cancel a timer by ID.
    void cancelTimer(uint32_t id);

    /// Called by Executor each loop. Fires all due timers.
    void processDueTimers(uint64_t now_us) {
        for (auto& t : timers_) {
            if (t.active && now_us >= t.next_fire_us) {
                t.callback(t.context);
                if (t.one_shot) {
                    t.active = false;
                } else {
                    t.next_fire_us += t.period_us;
                    // Guard against drift: if we missed multiple periods, skip ahead
                    if (t.next_fire_us < now_us) {
                        t.next_fire_us = now_us + t.period_us;
                    }
                }
            }
        }
    }

private:
    TimerEntry  timers_[MAX_TIMERS];
    uint32_t    next_id_ = 1;
};
```

### 8.2 Node Integration

```cpp
class PublishingNode : public Node {
protected:
    /// Create a periodic timer scoped to this node's lifetime
    uint32_t createTimer(uint32_t period_ms, TimerCallback cb) {
        return TimerManager::getInstance().createTimer(
            period_ms * 1000, cb, this);
    }

    /// Convenience: create a timer from a member function
    template<typename NodeT>
    uint32_t createTimer(uint32_t period_ms, void (NodeT::*method)()) {
        // Store trampoline in static array (no heap)
        // ...
    }
};
```

---

## 9. Service (Request/Response) Pattern

### 9.1 Design

Services provide synchronous-style request/response communication. On ESP32 single-core, the response is delivered in the same executor loop or the next one.

```cpp
/// Service request with a unique sequence number
template<typename RequestT, typename ResponseT>
struct ServiceTypes {
    using Request  = RequestT;
    using Response = ResponseT;
};

static constexpr size_t MAX_SERVICES = 8;
static constexpr size_t MAX_PENDING_REQUESTS = 4;

template<typename RequestT, typename ResponseT>
class ServiceServer {
public:
    using Handler = bool(*)(const RequestT& req, ResponseT& resp, void* context);

    ServiceServer(TopicId service_id, Handler handler, void* ctx)
        : service_id_(service_id), handler_(handler), context_(ctx) {}

    bool handleRequest(const RequestT& request, ResponseT& response) {
        return handler_(request, response, context_);
    }

private:
    TopicId  service_id_;
    Handler  handler_;
    void*    context_;
};

template<typename RequestT, typename ResponseT>
class ServiceClient {
public:
    enum class Status { PENDING, SUCCESS, TIMEOUT, ERROR };

    /// Send request. Response arrives via callback or poll.
    bool call(const RequestT& request, uint32_t timeout_ms = 100);

    /// Poll for response (non-blocking).
    Status getResponse(ResponseT& response);

private:
    TopicId   service_id_;
    uint32_t  sequence_;
    Status    status_;
    ResponseT pending_response_;
    uint64_t  deadline_us_;
};
```

### 9.2 Predefined Services

| Service | Request | Response | Use Case |
|---------|---------|----------|----------|
| `SetParameter` | `ParamSetRequest` | `ParamSetResponse` | Runtime config |
| `GetParameter` | `ParamGetRequest` | `ParamGetResponse` | Read config |
| `GetDiagnostics` | `DiagRequest` | `DiagResponse` | System health |
| `CalibrateServo` | `CalibRequest` | `CalibResponse` | Servo calibration |

---

## 10. Parameter Server

### 10.1 Design

A lightweight key-value store for runtime configuration. Parameters are stored in statically allocated memory with NVS (Non-Volatile Storage) persistence.

```cpp
static constexpr size_t MAX_PARAMETERS = 64;
static constexpr size_t MAX_PARAM_NAME_LEN = 24;

enum class ParamType : uint8_t {
    INT32, FLOAT, BOOL, STRING16  // STRING16 = 16-char fixed string
};

struct Parameter {
    char        name[MAX_PARAM_NAME_LEN];
    ParamType   type;
    union {
        int32_t   i;
        float     f;
        bool      b;
        char      s[16];
    } value;
    union {
        int32_t   i;
        float     f;
    } min_value, max_value;     // range constraints (int/float only)
    bool        persistent;     // save to NVS on change
    bool        has_range;
    uint32_t    change_count;   // incremented on every set
};

class ParameterServer {
public:
    static ParameterServer& getInstance();

    /// Declare a parameter with default value and optional range
    bool declare(const char* name, int32_t default_val,
                 int32_t min_val = INT32_MIN, int32_t max_val = INT32_MAX,
                 bool persistent = false);
    bool declare(const char* name, float default_val,
                 float min_val = -FLT_MAX, float max_val = FLT_MAX,
                 bool persistent = false);
    bool declare(const char* name, bool default_val, bool persistent = false);

    /// Get parameter value (returns false if not found or type mismatch)
    bool get(const char* name, int32_t& out) const;
    bool get(const char* name, float& out) const;
    bool get(const char* name, bool& out) const;

    /// Set parameter value (validates range, notifies listeners)
    bool set(const char* name, int32_t value);
    bool set(const char* name, float value);
    bool set(const char* name, bool value);

    /// Register callback for parameter changes
    using ChangeCallback = void(*)(const char* name, void* context);
    bool onChanged(const char* name, ChangeCallback cb, void* ctx);

    /// Load all persistent parameters from NVS
    void loadFromNVS();

    /// Save all dirty persistent parameters to NVS
    void saveToNVS();

    /// Introspection: iterate all parameters
    void forEach(void(*visitor)(const Parameter&, void*), void* ctx) const;

private:
    Parameter   params_[MAX_PARAMETERS];
    uint8_t     count_ = 0;

    struct ChangeListener {
        uint32_t       param_index;
        ChangeCallback callback;
        void*          context;
    };
    static constexpr size_t MAX_LISTENERS = 16;
    ChangeListener listeners_[MAX_LISTENERS];
    uint8_t        listener_count_ = 0;
};
```

### 10.2 Predefined Parameters

| Parameter | Type | Default | Range | Persistent | Description |
|-----------|------|---------|-------|------------|-------------|
| `drive.max_speed` | float | 0.8 | 0.0-1.0 | yes | Maximum drive speed |
| `drive.deadband` | float | 0.05 | 0.0-0.3 | yes | Joystick deadband |
| `drive.ramp_rate` | float | 3.0 | 0.1-10.0 | yes | Acceleration ramp rate |
| `dome.speed` | float | 0.6 | 0.0-1.0 | yes | Dome rotation speed |
| `audio.volume` | int32 | 128 | 0-255 | yes | Audio volume |
| `safety.timeout_ms` | int32 | 1000 | 100-5000 | yes | Motor safety timeout |
| `safety.enabled` | bool | true | -- | yes | Global safety enable |
| `debug.log_level` | int32 | 2 | 0-4 | no | Log verbosity |

---

## 11. Topic Introspection

### 11.1 Debug Shell Commands

Accessible via USB serial or web interface:

```
> topic list
  ID        NAME                 PRI  PUBS  SUBS  MSG/s   DROP  AVG_LAT
  0xA3F1    controller/input     128  1     2     50      0     42us
  0xB201    drive/cmd            192  1     1     50      0     18us
  0xC4E2    sensor/data          64   3     1     100     2     85us
  0xD100    system/status        128  1     3     1       0     120us

> topic info drive/cmd
  Topic: drive/cmd (0xB201)
  Priority: HIGH (192)
  QoS: RELIABLE, depth=5
  Publishers: 1 (DriveMapperNode)
  Subscribers: 1 (SabertoothNode)
  Messages: 15230 published, 0 dropped
  Latency: min=8us avg=18us max=142us p99=95us
  Histogram:
    [0-10us]    ######### 2105
    [10-50us]   ################################# 12800
    [50-100us]  ## 310
    [100-500us] # 15
    [>500us]    0

> topic echo drive/cmd --count 5
  [t=1042305] MotorCommand { motor_id=0, type=SET_SPEED, value=0.45 }
  [t=1062305] MotorCommand { motor_id=0, type=SET_SPEED, value=0.47 }
  ...

> param list
  NAME                  TYPE    VALUE   RANGE        PERSIST
  drive.max_speed       float   0.80    [0.0, 1.0]   yes
  drive.deadband        float   0.05    [0.0, 0.3]   yes
  safety.timeout_ms     int32   1000    [100, 5000]   yes

> param set drive.max_speed 0.6
  OK: drive.max_speed = 0.60 (was 0.80)
```

### 11.2 Web UI Endpoint (Future)

The parameter server and topic stats expose a simple JSON API served by the existing web configuration system:

```
GET /api/topics       -> array of TopicEntry stats
GET /api/params       -> array of Parameter values
POST /api/params      -> set parameter { "name": "...", "value": ... }
```

---

## 12. Revised Message Base Class

Replace RTTI-based type checking with compile-time type IDs:

```cpp
class Message {
public:
    Message() : timestamp_us_(0) {}
    virtual ~Message() = default;

    uint64_t  getTimestamp() const { return timestamp_us_; }
    void      setTimestamp(uint64_t ts) { timestamp_us_ = ts; }

    /// Compile-time type ID (FNV-1a of type name)
    virtual uint32_t getTypeId() const = 0;

    /// Human-readable type name (stored in flash)
    virtual const char* getTypeName() const = 0;

    /// Size in bytes
    virtual size_t getSize() const = 0;

private:
    uint64_t timestamp_us_;
};

/// CRTP base with automatic type ID generation
template<typename T>
class TypedMessage : public Message {
public:
    static constexpr uint32_t TYPE_ID = fnv1a(__PRETTY_FUNCTION__);

    uint32_t    getTypeId() const override   { return TYPE_ID; }
    const char* getTypeName() const override { return T::kTypeName; }
    size_t      getSize() const override     { return sizeof(T); }
};

/// Macro to define a message type with a static name
#define CHOPPER_MESSAGE(ClassName) \
    static constexpr const char* kTypeName = #ClassName;
```

Usage:

```cpp
class MotorCommand : public TypedMessage<MotorCommand> {
public:
    CHOPPER_MESSAGE(MotorCommand)
    uint8_t motor_id = 0;
    float   value = 0.0f;
    // ...
};
```

Type checking at subscription delivery becomes an integer comparison instead of `dynamic_cast`:

```cpp
bool handleMessage(SharedMessagePtr message) override {
    if (message->getTypeId() != MessageT::TYPE_ID) {
        incrementCounters(true);
        return false;
    }
    const MessageT* typed = static_cast<const MessageT*>(message.get());
    callback_(*typed);
    incrementCounters(false);
    return true;
}
```

---

## 13. Revised MessageBroker

### 13.1 Core API Changes

```cpp
class MessageBroker {
public:
    static MessageBroker& getInstance();

    /// Create publisher (returns index into static array)
    template<typename MessageT>
    PublisherHandle createPublisher(TopicId topic, uint8_t priority = 128,
                                   const QoSProfile& qos = QoSProfile::systemDefault());

    /// Create subscription (returns index into static array)
    template<typename MessageT>
    SubscriptionHandle createSubscription(TopicId topic,
                                          void(*callback)(const MessageT&, void*),
                                          void* context,
                                          const QoSProfile& qos = QoSProfile::systemDefault());

    /// Publish a message (zero-copy path)
    template<typename MessageT>
    bool publish(PublisherHandle handle, const MessageT& msg);

    /// Process pending messages (called by Executor)
    void processPending(uint64_t budget_us);

    /// Get topic statistics
    const TopicEntry* getTopicInfo(TopicId topic) const;

    /// Introspection iterator
    void forEachTopic(void(*visitor)(const TopicEntry&, void*), void* ctx) const;

private:
    TopicRegistry       registry_;
    PriorityDispatcher  dispatcher_;
    TimerManager        timers_;

    // Static pool for common message types
    MessagePool<ControllerInput, 4>  controller_pool_;
    MessagePool<MotorCommand, 16>    motor_pool_;
    MessagePool<ServoCommand, 16>    servo_pool_;
    MessagePool<SensorData, 16>      sensor_pool_;
};
```

### 13.2 Handle Types

Lightweight, non-owning handles instead of `shared_ptr`:

```cpp
struct PublisherHandle {
    uint8_t topic_index;
    uint8_t publisher_index;
    uint16_t generation;  // ABA protection

    bool isValid() const;
};

struct SubscriptionHandle {
    uint8_t topic_index;
    uint8_t subscription_index;
    uint16_t generation;

    bool isValid() const;
};
```

---

## 14. Memory Budget

### 14.1 Total Static Allocation

| Component | Size | Notes |
|-----------|------|-------|
| TopicRegistry (32 entries) | 32 x 128 = 4,096 B | Topics + subscriber slots |
| Message Pools | ~1,536 B | See Section 5.2 |
| SPSC Rings (32 subscriptions x 8 deep) | 32 x 8 x 8 = 2,048 B | Pointer-sized entries |
| TimerManager (16 timers) | 16 x 32 = 512 B | |
| ParameterServer (64 params) | 64 x 56 = 3,584 B | |
| LatencyStats (32 topics) | 32 x 48 = 1,536 B | |
| ServiceRegistry (8 services) | 8 x 64 = 512 B | |
| **Total** | **~13.8 KB** | ~4.3% of 320 KB SRAM |

### 14.2 Comparison with Current Design

| Aspect | Current | Proposed |
|--------|---------|----------|
| Topic lookup | `unordered_map` (heap) | Static array with hash (no heap) |
| Message allocation | `make_shared` per publish | Static pool, zero-copy |
| Subscription queues | None (synchronous callback) | Lock-free SPSC ring |
| Type checking | `dynamic_cast` (RTTI) | Integer comparison |
| Topic names | `std::string` (heap) | Flash-stored `const char*` + hash |
| Priority support | None | 4-level priority dispatch |

---

## 15. Migration Path

### Phase 1: Foundation (Non-Breaking)

1. Add `fnv1a()` hash utility and `TopicId` type
2. Add `CHOPPER_MESSAGE` macro to existing messages (alongside existing `TypedMessage`)
3. Implement `MessagePool` template
4. Implement `SPSCRing` template
5. Implement `ParameterServer` with NVS backend

### Phase 2: Broker Upgrade (Breaking for Internal API)

1. Replace `MessageBroker` internals with `TopicRegistry` + `PriorityDispatcher`
2. Replace `shared_ptr<Publisher>` with `PublisherHandle`
3. Replace `dynamic_cast` with `getTypeId()` comparison
4. Update `PublishingNode` convenience methods to use new API

### Phase 3: New Features

1. Add `TimerManager` integration into `Executor`
2. Add `ServiceServer` / `ServiceClient`
3. Add debug shell commands for topic introspection
4. Add web API for parameter access

### Phase 4: Optimization

1. Profile and tune pool sizes based on actual usage
2. Add compile-time topic validation (static_assert for duplicate hashes)
3. Optional: SMP support with per-core dispatch queues

---

## 16. ESP32-Specific Considerations

| Concern | Mitigation |
|---------|------------|
| Single core (ESP32 with BT uses core 0 for BT) | All message processing on core 1; lock-free SPSC avoids cross-core synchronization |
| 320 KB SRAM | Total message system uses ~14 KB (4.3%); pools sized conservatively |
| No MMU | Static allocation prevents fragmentation; pool exhaustion is detectable |
| Cache lines (32 bytes on Xtensa) | SPSC ring head/tail separated by `alignas(32)` to avoid false sharing |
| NVS wear leveling | Parameter saves batched; only write on explicit save or graceful shutdown |
| `esp_timer_get_time()` precision | 1 us resolution, sufficient for latency tracking |
| Flash string storage | Topic names and parameter names use `PROGMEM`-equivalent (`.rodata` section) |
| ISR safety | Message pools use `atomic` operations compatible with ISR context for CRITICAL priority topics |

---

## 17. Example Usage

### 17.1 Publishing a Motor Command

```cpp
class DriveMapperNode : public PublishingNode {
public:
    DriveMapperNode() : PublishingNode("drive_mapper") {}

    bool initialize() override {
        drive_pub_ = createPublisher<MotorCommand>(
            topics::TOPIC_drive_cmd, /* priority */ 192);
        controller_sub_ = createSubscription<ControllerInput>(
            topics::TOPIC_controller_input,
            &DriveMapperNode::onControllerInput, this);

        ParameterServer::getInstance().declare("drive.max_speed", 0.8f, 0.0f, 1.0f, true);
        return true;
    }

    void process(uint64_t now) override { /* timers handle periodic work */ }
    void emergencyStop() override { /* handled by safety system */ }

private:
    void onControllerInput(const ControllerInput& input) {
        float max_speed;
        ParameterServer::getInstance().get("drive.max_speed", max_speed);

        MotorCommand cmd;
        cmd.motor_id = 0;
        cmd.command_type = MotorCommand::CommandType::SET_SPEED;
        cmd.value = input.axis_y_normalized * max_speed;
        drive_pub_->publish(cmd);
    }

    PublisherHandle drive_pub_;
    SubscriptionHandle controller_sub_;
};
```

### 17.2 Using Services

```cpp
// Server side (in a calibration node)
bool handleCalibrate(const CalibRequest& req, CalibResponse& resp, void* ctx) {
    auto* node = static_cast<CalibrationNode*>(ctx);
    resp.success = node->calibrateServo(req.servo_id);
    resp.min_us = node->getMinPulse(req.servo_id);
    resp.max_us = node->getMaxPulse(req.servo_id);
    return true;
}

// Client side (in a setup node)
ServiceClient<CalibRequest, CalibResponse> calib_client(topics::SVC_CALIBRATE);
CalibRequest req{.servo_id = 3};
calib_client.call(req, /* timeout_ms */ 500);
// ... later in process():
CalibResponse resp;
if (calib_client.getResponse(resp) == ServiceClient::Status::SUCCESS) {
    // use resp.min_us, resp.max_us
}
```

---

## 18. Open Questions

1. **Pool sizing strategy**: Should pool sizes be configurable per-project via a `chopper_config.h`, or fixed?
2. **Multi-core dispatch**: If BT is pinned to core 0, should the message broker support cross-core queues from the BT task?
3. **Topic wildcards**: Should we support hierarchical topic matching (e.g., `sensor/*`)? This adds complexity.
4. **Message serialization**: For future multi-ESP32 networking, should messages have a `serialize()`/`deserialize()` interface now, or defer?
5. **QoS negotiation**: When publisher and subscriber declare different QoS, which wins? Proposal: stricter (higher reliability) wins.
