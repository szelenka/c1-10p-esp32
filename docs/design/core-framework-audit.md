# Core Framework Architecture Audit

**Date**: 2026-02-27
**Scope**: `chopper::core` namespace -- Executor, Node, MessageBroker, Publisher, Subscription, Message, PublishingNode
**Target platform**: ESP32 (Xtensa LX6, single-core or dual-core, ~320 KB SRAM, FreeRTOS)

## Status Note (2026-02-28)

This audit contains findings captured on 2026-02-27. Some findings are now superseded by current code:
- `Executor` uses `vTaskDelayUntil()` in `main/chopper/core/Executor.cpp`.
- `Node::last_process_time_` is updated after node processing in `Executor::processNodes()`.
- `Executor::emergencyStop()` now uses `const char*` reason (no `std::string` allocation).

Treat this document as historical analysis until a full refresh is completed.

---

## 1. Current State Assessment

### 1.1 Architecture Overview

The framework follows a ROS2-inspired publish/subscribe pattern adapted for ESP32:

| Component | Role | File |
|-----------|------|------|
| `Node` | Base unit of computation with lifecycle states | `core/Node.h`, `core/Node.cpp` |
| `PublishingNode` | Node subclass with pub/sub convenience methods | `core/PublishingNode.h` |
| `Message` / `TypedMessage<T>` | Base and CRTP wrapper for typed messages | `core/Message.h` |
| `Publisher` / `TypedPublisher<T>` | Sends messages; maintains subscriber list | `core/Publisher.h` |
| `Subscription` / `TypedSubscription<T>` | Receives messages via callback | `core/Subscription.h` |
| `MessageBroker` (singleton) | Topic-based registry connecting publishers and subscriptions | `core/MessageBroker.h`, `core/MessageBroker.cpp` |
| `Executor` | FreeRTOS task driving the main loop, frequency scheduling, safety watchdogs | `core/Executor.h`, `core/Executor.cpp` |
| `QoSProfile` | Reliability/history hints (currently advisory, not enforced) | `core/Message.h` |

The system currently runs in a single FreeRTOS task at the highest priority. Nodes declare an update frequency; the executor polls each node in a round-robin loop and skips nodes whose period has not yet elapsed.

### 1.2 What Works Well

1. **Clear separation of concerns** -- Nodes are single-purpose, decoupled through topics.
2. **Lifecycle state machine** -- INACTIVE -> ACTIVE -> PAUSED -> ERROR -> SHUTDOWN provides a foundation for managed startup/shutdown.
3. **Safety subsystem** -- Watchdog, per-node execution-time budgets, and an emergency-stop chain are present from the start.
4. **Adapter pattern for controllers** -- `IControllerSource` cleanly decouples Bluepad32 from the node graph.
5. **CRTP `TypedMessage<T>`** -- Gives type-safe publish/subscribe with automatic `clone()` and `getSize()`.
6. **QoS profiles** -- Named presets (`sensorData`, `commandAndControl`) establish intent even though enforcement is incomplete.

### 1.3 Code Metrics (header-only + implementation)

| Artifact | Approximate size |
|----------|-----------------|
| `ControllerInput` message | ~164 bytes per instance |
| `MotorCommand` message | ~16 bytes |
| `ServoCommand` message | ~16 bytes |
| `SensorData` message | ~16 bytes |
| `SystemStatus` message | ~76 bytes |
| `AudioCommand` / `LEDCommand` | ~12-16 bytes each |
| `Executor::Statistics` struct | 56 bytes |
| `MessageBroker` singleton overhead | ~128 bytes base + per-topic vectors + unordered_maps |
| Per-node base overhead (`Node` + vtable + string) | ~48-80 bytes + name string allocation |

---

## 2. Identified Issues and Risks

### 2.1 CRITICAL -- Heap Allocation on Every Publish

**Location**: `TypedPublisher<T>::publish()` at `Publisher.h:120-123`

```cpp
bool publish(const MessageT& message) {
    auto shared_msg = std::make_shared<MessageT>(message);  // HEAP ALLOC
    shared_msg->setTimestamp(esp_timer_get_time());
    return Publisher::publish(shared_msg);
}
```

Every publish call triggers `std::make_shared`, which allocates on the heap. At 50 Hz controller input and potentially multiple other publishers, this creates:
- **Heap fragmentation** over time on a 320 KB system.
- **Non-deterministic latency** from the allocator.
- **Violation of the stated design goal**: "no dynamic allocation in critical paths" (DEVELOPMENT_PLAN.md, line 109).

**Risk**: Memory fragmentation leading to eventual allocation failure after hours of operation.

**Estimated impact**: ~200-500 bytes allocated + freed per publish cycle, multiplied by number of active publishers.

### 2.2 CRITICAL -- `std::string` Use Throughout Hot Paths

**Locations**: `Node::logInfo/logWarning/logError`, `emergencyStop(const std::string& reason)`, topic names stored as `std::string` in publishers/subscriptions.

Each log call in `Node.cpp` constructs temporary `std::string` objects:
```cpp
logInfo("State transition: " + std::string(old_state_str) + " -> " + std::string(new_state_str));
```

On ESP32, `std::string` concatenation allocates on the heap. In the state-transition path (which can happen during emergency stop), this risks heap allocation failure at the worst possible time.

**Risk**: Allocation failure during emergency stop, defeating the safety system.

### 2.3 HIGH -- `std::unordered_map` in MessageBroker

**Location**: `MessageBroker.h:132-135`

```cpp
std::unordered_map<std::string, std::vector<std::weak_ptr<Publisher>>> publishers_by_topic_;
std::unordered_map<std::string, std::vector<std::weak_ptr<Subscription>>> subscriptions_by_topic_;
```

`std::unordered_map` has significant overhead on ESP32:
- Hash table bucket array (~4-8 bytes per bucket, default load factor creates many buckets).
- Each entry is a heap-allocated node with forward/backward pointers.
- Rehashing is expensive and non-deterministic.

For a system with 5-15 topics, a flat array with linear search would use less memory, have better cache locality, and be fully deterministic.

**Estimated overhead**: ~500-2000 bytes for the hash maps alone with typical topic counts, plus ongoing fragmentation from bucket rehashing.

### 2.4 HIGH -- `std::mutex` Used in Single-Task Executor

**Locations**: `Executor.h:197` (`stats_mutex_`), `MessageBroker.h:144` (`stats_mutex_`)

The executor runs all nodes in a single FreeRTOS task. Mutexes are only needed if statistics are read from another task (e.g., a monitoring/telemetry task). Currently:
- `updateStatistics()` locks a mutex on every loop iteration (1000 Hz by default).
- `getStatistics()` also locks.

On single-core ESP32, `std::mutex` maps to a FreeRTOS mutex, which involves kernel calls and priority-inheritance checks -- overhead that is unnecessary when only one task touches the data.

**Risk**: ~2-5 us overhead per loop iteration from unnecessary mutex operations.

### 2.5 HIGH -- No Message Queue / Buffering (Synchronous Delivery)

**Location**: `MessageBroker::processPendingMessages()` at `MessageBroker.cpp:81-92`

```cpp
void MessageBroker::processPendingMessages() {
    // In this implementation, messages are delivered immediately in publish()
    // This function could be used for batched delivery or message queue processing
    // For now, we'll use it for cleanup
```

Messages are delivered synchronously inside `Publisher::deliver()` at publish time. This means:
- A slow subscription callback blocks the publishing node and the entire executor loop.
- There is no way to implement QoS depth > 1 or KEEP_ALL history.
- Priority-based delivery (stated in MessageBroker header comments) cannot work.
- The `processPendingMessages()` call in the executor loop does essentially nothing useful (only periodic cleanup).

**Risk**: Unpredictable loop timing when a subscription callback takes longer than expected.

### 2.6 HIGH -- `dynamic_cast` in Message Delivery Hot Path

**Location**: `TypedSubscription<T>::handleMessage()` at `Subscription.h:129`

```cpp
const MessageT* typed_msg = dynamic_cast<const MessageT*>(message.get());
```

`dynamic_cast` requires RTTI (Run-Time Type Information), which:
- Adds ~1-2 KB per polymorphic class to the binary (vtable + type_info structures).
- Has non-trivial runtime cost (string comparison of type names in many implementations).
- Is already redundant here because `matchesMessageType()` does a `strcmp` check on `typeid().name()` just before delivery in `Publisher::deliver()`.

The type is checked twice: once by `Publisher::deliver()` via `matchesMessageType()` and again by `dynamic_cast` in `handleMessage()`.

### 2.7 HIGH -- `typeid().name()` for Type Matching is Fragile

**Location**: `TypedMessage<T>::getTypeName()` at `Message.h:82-84`, `Subscription::matchesMessageType()` at `Subscription.cpp:15-17`

```cpp
const char* getTypeName() const override {
    return typeid(T).name();
}
```

`typeid(T).name()` is implementation-defined. On GCC (used by ESP-IDF), it returns mangled names. This works as long as publisher and subscriber are compiled in the same translation unit with the same compiler, which is currently guaranteed. However:
- It makes cross-module or cross-process messaging impossible in the future.
- Mangled names are long strings, making `strcmp` comparisons unnecessarily slow.

### 2.8 MEDIUM -- Executor Frequency Scheduling is Imprecise

**Location**: `Executor::executionLoop()` at `Executor.cpp:240-257`

```cpp
// Calculate sleep time to maintain frequency
next_loop_time += loop_period_us;
int64_t sleep_time_us = next_loop_time - esp_timer_get_time();
if (sleep_time_us > 0) {
    TickType_t sleep_ticks = pdMS_TO_TICKS(sleep_time_us / 1000);
    if (sleep_ticks == 0) sleep_ticks = 1;
    vTaskDelay(sleep_ticks);
}
```

Problems:
- `vTaskDelay` has 1-tick granularity (typically 1 ms with configTICK_RATE_HZ=1000). For a 1000 Hz loop, the sleep calculation `sleep_time_us / 1000` truncates sub-millisecond remainders, causing systematic drift.
- At 1000 Hz with a 1 ms tick, the minimum sleep is 1 tick = 1 ms, which means the loop cannot achieve better than ~500 Hz effective rate when sleep is needed.
- `vTaskDelayUntil` would be more appropriate for periodic execution (it compensates for execution time automatically).

### 2.9 MEDIUM -- Node Frequency Check Uses Floating-Point Division Per Iteration

**Location**: `Executor::processNodes()` at `Executor.cpp:276-282`

```cpp
double frequency = node->getUpdateFrequency();
if (frequency > 0.0) {
    uint64_t period_us = static_cast<uint64_t>(1000000.0 / frequency);
    ...
}
```

This performs a double-precision division on every loop iteration for every node. ESP32 has no hardware FPU for doubles (only single-precision float). This should be pre-computed once during node registration or activation.

### 2.10 MEDIUM -- `std::vector<std::weak_ptr<Subscription>>` Cleanup During Delivery

**Location**: `Publisher::deliver()` at `Publisher.cpp:59-81`

The `deliver()` method erases expired weak_ptrs from the vector during iteration using `erase()`. This causes O(n) element shifts on each removal. With many subscriptions coming and going (though unlikely in this use case), this could cause unexpected latency spikes.

### 2.11 MEDIUM -- MessageBroker Singleton Pattern

**Location**: `MessageBroker::getInstance()` at `MessageBroker.cpp:10-13`

The singleton pattern:
- Makes unit testing difficult (cannot inject mock brokers).
- Prevents running multiple isolated node graphs (useful for testing or sandboxing).
- Creates hidden global state.

### 2.12 MEDIUM -- `ControllerInput` Message is Very Large

**Location**: `CommonMessages.h:11-80`

`ControllerInput` is approximately 164 bytes. It is published at 50 Hz (20 ms interval). With `std::make_shared`, each publish allocates ~200 bytes (164 + shared_ptr control block). This means:
- ~10 KB/s of allocation throughput just for controller input.
- The `hasSignificantChange()` filter in `ControllerInputNode` mitigates this somewhat, but still copies the full 164-byte struct for comparison.

### 2.13 LOW -- Exception Handling in Real-Time Path

**Locations**: `Executor::processNodes()` at `Executor.cpp:288-294`, `TypedSubscription::handleMessage()` at `Subscription.h:137-144`

```cpp
try {
    node->process(now);
} catch (...) {
```

Exception handling on ESP32 (Xtensa) has a significant binary size cost (~10-20 KB) and non-trivial runtime overhead for the unwind tables. The ESP-IDF build system supports disabling exceptions (`-fno-exceptions`), and many ESP32 projects do so. If exceptions are disabled, these `try/catch` blocks become dead code or compile errors.

### 2.14 LOW -- `QoSProfile` Is Declared But Not Enforced

The `QoSProfile` struct defines `Reliability`, `History`, `depth`, and `max_memory_kb`, but:
- `RELIABLE` vs `BEST_EFFORT` has no implementation difference.
- `KEEP_LAST` with `depth > 1` is not enforced (no queue).
- `max_memory_kb` is never checked.

This is not a bug per se (the code works), but it sets user expectations that cannot currently be met.

### 2.15 LOW -- `const_cast` in Publisher::publish()

**Location**: `Publisher.cpp:24`

```cpp
const_cast<Message*>(message.get())->setTimestamp(esp_timer_get_time());
```

This `const_cast` on a `shared_ptr<const Message>` is technically undefined behavior if the original object was const-qualified. It also indicates an API design issue -- the timestamp should be set before the message enters the const shared_ptr.

### 2.16 CRITICAL -- `last_process_time_` Is Never Updated (Frequency Gating Broken)

**Location**: `Node.h:153` (declaration), `Executor.cpp:266-311` (usage)

`Node::last_process_time_` is initialized to 0 in `Node.cpp:15` and never assigned after that. The frequency gating logic in `Executor::processNodes()`:

```cpp
uint64_t last_process = node->getLastProcessTime();
if (last_process > 0 && (now - last_process) < period_us) {
    continue; // Skip this node for now
}
```

Since `last_process` is always 0, the condition `last_process > 0` is always false. Every node runs on every single tick regardless of its configured `getUpdateFrequency()`. The 50 Hz ControllerInputNode and 10 Hz SensorNode both execute at the full executor rate.

**Impact**: Wasted CPU cycles, incorrect timing assumptions for all frequency-dependent nodes.

**Fix**: After `node->process(now)` returns in `Executor::processNodes()`, update the node's last process time. This requires either making `Executor` a friend of `Node` or adding a public setter.

### 2.17 HIGH -- `vTaskDelay` Resolution Cannot Achieve 1 kHz Loop

**Location**: `Executor.cpp:246-248`

```cpp
TickType_t sleep_ticks = pdMS_TO_TICKS(sleep_time_us / 1000);
if (sleep_ticks == 0) sleep_ticks = 1;
vTaskDelay(sleep_ticks);
```

With the default ESP-IDF `configTICK_RATE_HZ = 100`, one FreeRTOS tick = 10 ms. The Executor defaults to `loop_frequency_hz = 1000` (1 ms period). After processing, `sleep_time_us` might be 800 us. `pdMS_TO_TICKS(800/1000) = pdMS_TO_TICKS(0) = 0`, so `sleep_ticks = 1` (forced minimum). One tick = 10 ms. The actual loop runs at approximately 100 Hz, not 1000 Hz.

Even with `configTICK_RATE_HZ = 1000`, `vTaskDelay(1)` sleeps for 1 ms, making the loop ceiling roughly 500 Hz (each iteration: process + 1 ms sleep). Using `vTaskDelayUntil` instead would help, and reducing the default to 100 Hz (which is sufficient for 50 Hz controllers) would align with the tick rate.

### 2.18 HIGH -- Executor Flags Not Atomic

**Location**: `Executor.h:188-189`

```cpp
bool is_running_;
bool should_stop_;
bool emergency_stop_;
```

These flags are read and written from different contexts:
- `should_stop_` is set in `stop()` (caller's task) and read in `executionLoop()` (executor task).
- `is_running_` is set in `start()` and `stop()` (caller's task), read in `isRunning()` (any task).
- `emergency_stop_` is set in `emergencyStop()` (potentially any task), read in `executionLoop()`.

Without `std::atomic<bool>`, the compiler may optimize reads into registers and never reload from memory, causing the executor task to spin indefinitely after `stop()` is called.

**Fix**: Change to `std::atomic<bool>` for all three flags.

### 2.19 MEDIUM -- Executor::stop() Force-Deletes Task

**Location**: `Executor.cpp:173-183`

```cpp
if (eTaskGetState(task_handle_) != eDeleted) {
    vTaskDelete(task_handle_);
}
```

`vTaskDelete()` from outside the target task is dangerous if the task holds a mutex, is in a critical section, or has allocated temporary resources on the stack. The `taskWrapper` already calls `vTaskDelete(nullptr)` after `executionLoop()` returns, so a graceful stop would be: set `should_stop_`, wait for `is_running_` to become false (with timeout), and only force-delete as a last resort.

### 2.20 MEDIUM -- addNode/removeNode Not Thread-Safe

**Location**: `Executor.cpp:32-75`

`addNode()` and `removeNode()` modify `nodes_` (a `std::vector`), which is iterated by `processNodes()` in the executor task. If these are called from a different task while the executor is running, the vector could be modified during iteration, causing undefined behavior.

**Recommendation**: Document that all `addNode()`/`removeNode()` calls must occur before `start()`, or add a lock-free pending queue.

### 2.21 MEDIUM -- QoS Static Method Defined in Both Header and .cpp

**Location**: `Message.h:123-136` and `Message.cpp:7-20`

`QoSProfile::systemDefault()`, `sensorData()`, and `commandAndControl()` are defined inline in the header as static member functions with static local variables, AND also defined in `Message.cpp`. This is an ODR (One Definition Rule) violation because the two definitions have different `max_memory_kb` values (`64` in header vs `128` in .cpp for `commandAndControl`). Which definition is used depends on the linker.

**Fix**: Remove one set of definitions. Keep the .cpp versions as the single source of truth, and declare-only in the header.

---

## 3. Recommended Improvements

### 3.1 Replace Heap Allocation with Memory Pool for Messages (Priority: CRITICAL)

**Rationale**: Eliminates the single largest source of heap fragmentation and non-determinism.

**Approach**: Implement a fixed-size memory pool per message type (or a small number of size-class pools).

```
// Conceptual API
template<typename T, size_t PoolSize = 4>
class MessagePool {
    alignas(T) uint8_t storage_[PoolSize][sizeof(T)];
    std::bitset<PoolSize> in_use_;
public:
    T* allocate();
    void deallocate(T* ptr);
};
```

Each `TypedPublisher<T>` would own a small pool (2-8 slots). The publish method would acquire a slot, placement-new the message, and return a pool-aware smart pointer that releases the slot on destruction.

**Memory impact**: For `ControllerInput` (164 bytes) with pool size 4: 656 bytes statically allocated. This replaces unbounded heap allocation with a fixed, predictable cost.

**Performance impact**: Pool allocation is O(1) with a bitfield scan. No heap fragmentation.

### 3.2 Replace `std::string` with Fixed-Size Identifiers (Priority: CRITICAL)

**Rationale**: Eliminates heap allocation from node names and topic identifiers.

**Approach**:
- Node names: `char name_[32]` fixed buffer, or a compile-time string hash (`uint32_t name_hash_`).
- Topic identifiers: Use an enum or `uint16_t` topic ID instead of string-based topics. A static registry maps IDs to human-readable names for debugging only.
- Log messages: Use `ESP_LOGI` with `const char*` format strings directly, avoiding `std::string` concatenation.

```
// Instead of:
logInfo("State transition: " + std::string(old_state_str) + " -> " + std::string(new_state_str));

// Use:
ESP_LOGI(TAG, "[%s] State transition: %s -> %s", name_, old_state_str, new_state_str);
```

**Memory impact**: Saves ~24 bytes per string object + heap allocations. Node name limited to 32 chars instead of unbounded.

### 3.3 Replace `std::unordered_map` with Static Flat Arrays (Priority: HIGH)

**Rationale**: The number of topics is small and known at compile time or system init. Hash maps are overkill.

**Approach**: A fixed-capacity `StaticMap<Key, Value, MaxEntries>` using linear search:

```
template<typename K, typename V, size_t N>
struct StaticMap {
    struct Entry { K key; V value; bool occupied; };
    Entry entries[N];
    size_t count = 0;
    // ...
};
```

With N=16 topics, linear search over 16 entries is faster than hash computation + bucket traversal for small N, and uses zero heap memory.

**Memory impact**: Saves ~500-2000 bytes vs unordered_map. Fully deterministic.

### 3.4 Implement Proper Message Queuing (Priority: HIGH)

**Rationale**: Decouples publisher timing from subscriber execution, enables QoS enforcement, and makes loop timing predictable.

**Approach**: Each subscription owns a lock-free ring buffer of message pointers:

```
template<typename T, size_t Depth = 4>
class SubscriptionQueue {
    T* slots_[Depth];
    std::atomic<size_t> head_, tail_;
public:
    bool push(T* msg);  // returns false if full (BEST_EFFORT drops, RELIABLE blocks)
    T* pop();
};
```

The publish path enqueues; `processPendingMessages()` dequeues and invokes callbacks. This:
- Makes the publish call O(1) and non-blocking.
- Allows QoS depth to actually work.
- Separates the timing of producers and consumers.

**Memory impact**: Depth * sizeof(pointer) per subscription = 16-32 bytes for depth 4.

### 3.5 Replace `dynamic_cast` with Compile-Time Type Tags (Priority: HIGH)

**Rationale**: Eliminates RTTI overhead and double type-checking.

**Approach**: Assign each message type a unique `uint16_t` type ID at compile time:

```
// In each message class:
static constexpr uint16_t TYPE_ID = 0x0001;
uint16_t getTypeId() const override { return TYPE_ID; }
```

Or use a hash of the type name computed at compile time:

```
template<typename T>
constexpr uint16_t type_id() {
    return static_cast<uint16_t>(compile_time_hash(__PRETTY_FUNCTION__));
}
```

This replaces `strcmp` + `dynamic_cast` with a single integer comparison.

**Binary size impact**: Disabling RTTI (`-fno-rtti`) saves ~1-2 KB per polymorphic type, potentially 10-20 KB total.

### 3.6 Use `vTaskDelayUntil` for Periodic Execution (Priority: HIGH)

**Rationale**: Corrects systematic timing drift and simplifies the sleep calculation.

```cpp
TickType_t last_wake_time = xTaskGetTickCount();
const TickType_t period_ticks = pdMS_TO_TICKS(1000 / config_.loop_frequency_hz);

while (!should_stop_) {
    // ... process ...
    vTaskDelayUntil(&last_wake_time, period_ticks);
}
```

For sub-millisecond precision, consider using an `esp_timer` callback instead of a FreeRTOS task, which provides microsecond-resolution scheduling.

**Performance impact**: Eliminates jitter from the current manual sleep calculation.

### 3.7 Pre-compute Node Periods (Priority: MEDIUM)

**Rationale**: Avoids double-precision division on every loop iteration.

**Approach**: Compute and cache the period in microseconds when a node is added or activated:

```
struct NodeEntry {
    NodePtr node;
    uint64_t period_us;       // pre-computed from getUpdateFrequency()
    uint64_t next_run_time;   // next scheduled execution
};
```

The executor loop becomes a simple integer comparison: `if (now >= entry.next_run_time)`.

**Performance impact**: Replaces a `double` division per node per loop with an integer comparison. On ESP32 without hardware double FPU, this saves ~100-200 cycles per node per loop.

### 3.8 Make MessageBroker Injectable (Priority: MEDIUM)

**Rationale**: Enables testing and multi-graph scenarios.

**Approach**: Pass `MessageBroker&` to `PublishingNode` constructor or through the `Executor`. Keep a default global instance for convenience but allow override.

```
class PublishingNode : public Node {
public:
    explicit PublishingNode(const std::string& name,
                            MessageBroker& broker = MessageBroker::getDefault());
};
```

### 3.9 Add Timer / Periodic Callback Support to Nodes (Priority: MEDIUM)

**Rationale**: Currently, periodic behavior is implemented via `getUpdateFrequency()` and the executor polling every node on every loop iteration. This couples scheduling to the main loop frequency and wastes cycles checking nodes that should not run.

**Approach**: Let nodes register timer callbacks at specific frequencies, managed by the executor:

```
class Node {
protected:
    TimerHandle createTimer(uint32_t period_ms, std::function<void()> callback);
    void cancelTimer(TimerHandle handle);
};
```

This is a common ROS2 pattern that decouples node timing from the executor loop rate.

### 3.10 Add Parameter / Configuration System (Priority: LOW)

**Rationale**: Currently, node configuration (e.g., `max_drive_speed_`, `ANALOG_THRESHOLD`) is hardcoded or set via setter methods. A lightweight parameter system would allow runtime tuning without recompilation.

**Approach**: A flat key-value store per node with typed accessors:

```
class Node {
protected:
    void declareParameter(const char* name, float default_value);
    float getParameter(const char* name) const;
    void setParameter(const char* name, float value);
};
```

Backed by a fixed-size array of `{hash, value}` pairs (no heap allocation).

### 3.11 Add Service (Request/Reply) Pattern (Priority: LOW)

**Rationale**: Some interactions are naturally request/reply (e.g., "get current dome position", "query controller battery level"). Currently these require publishing a request message and subscribing to a response topic, which is awkward.

**Approach**: A lightweight service wrapper:

```
template<typename RequestT, typename ResponseT>
class Service {
    using Handler = std::function<ResponseT(const RequestT&)>;
};
```

This is lower priority because the pub/sub model handles most robotics use cases.

---

## 4. API Evolution Suggestions

### 4.1 Short-term (Next sprint): Zero-allocation publish path

Change `TypedPublisher<T>::publish()` to avoid heap allocation:

```cpp
// Option A: Stack-based with intrusive reference count
bool publish(const MessageT& message) {
    // Deliver synchronously using const reference (no copy needed
    // when delivery is synchronous within a single task)
    MessageT stamped = message;
    stamped.setTimestamp(esp_timer_get_time());
    deliverDirect(stamped);  // New method: passes const ref to each subscriber
    return true;
}

// Option B: Pool-based (needed if queueing is added)
bool publish(const MessageT& message) {
    auto* slot = pool_.allocate();
    if (!slot) return false;  // pool exhausted = backpressure
    new (slot) MessageT(message);
    slot->setTimestamp(esp_timer_get_time());
    enqueue(slot);  // Into per-subscriber ring buffers
    return true;
}
```

### 4.2 Short-term: Replace string topics with integer IDs

```cpp
// topics.h (generated or hand-maintained)
namespace topics {
    constexpr uint16_t CONTROLLER_INPUT = 1;
    constexpr uint16_t MOTOR_COMMAND = 2;
    constexpr uint16_t SERVO_COMMAND = 3;
    // ...
}
```

Existing string-based API can remain as a convenience wrapper that hashes to the integer ID at registration time.

### 4.3 Medium-term: Executor with priority groups

Instead of a flat list, group nodes by priority:

```
enum class Priority { SAFETY, CONTROL, SENSOR, TELEMETRY };

executor.addNode(e_stop_node, Priority::SAFETY);
executor.addNode(drive_node, Priority::CONTROL);
executor.addNode(sensor_node, Priority::SENSOR);
```

Safety nodes always run. Control nodes run at their declared frequency. Sensor/telemetry nodes run only if time budget remains.

### 4.4 Long-term: Compile-time node graph wiring

For maximum static analysis and zero-overhead topic matching:

```cpp
// Declare the graph at compile time
using MyGraph = NodeGraph<
    Connection<ControllerInputNode, topics::CONTROLLER_INPUT, DriveControlNode>,
    Connection<DriveControlNode, topics::MOTOR_COMMAND, MotorControlNode>
>;
```

This eliminates the runtime broker entirely for known topologies, while still allowing dynamic topics for extensibility.

---

## 5. Thread Safety Analysis: Single-Core vs Dual-Core ESP32

### 5.1 Current State

The framework currently uses:
- `std::mutex` in `Executor` (stats) and `MessageBroker` (stats).
- No other synchronization primitives.
- All node processing happens in a single FreeRTOS task.

### 5.2 Single-Core ESP32 (ESP32-S2, ESP32-C3)

On single-core, FreeRTOS is cooperative (preemption only at tick boundaries or explicit yields). Since all nodes run in one task, there are no data races. The mutexes are pure overhead.

**Recommendation**: Guard mutex usage behind a `#if CONFIG_FREERTOS_UNICORE` / `#else` preprocessor check, or use atomic operations for statistics counters.

### 5.3 Dual-Core ESP32 (Original ESP32, ESP32-S3)

If the executor task runs on one core and a monitoring/telemetry task runs on another:
- Statistics reads require synchronization (current mutexes are correct for this).
- Message delivery is NOT thread-safe: `Publisher::deliver()` iterates `subscriptions_` without locks. If subscriptions are added/removed from another task, this is a data race.
- `MessageBroker::registerPublisher/registerSubscription` modify shared vectors without locks.

**Recommendation**: If multi-core operation is planned:
1. Use a read-write lock or make the subscription list copy-on-write.
2. Add `portENTER_CRITICAL` / `portEXIT_CRITICAL` around broker registration operations.
3. Consider pinning the executor to one core and all I/O to the other (common ESP32 pattern).

### 5.4 Recommended Default: Single-Task Model

For a robotics puppet controller, single-task operation with cooperative scheduling is the simplest and most deterministic approach. Reserve multi-core for specific, well-isolated workloads (e.g., WiFi/BT stack on core 0, robot logic on core 1 -- which ESP-IDF already does by default for the BT stack).

---

## 6. Template Bloat Risk Assessment

### 6.1 Current Template Instantiations

Each unique message type used with `TypedPublisher<T>`, `TypedSubscription<T>`, and `TypedMessage<T>` generates a separate class instantiation. Current message types in `CommonMessages.h`:

| Message Type | `TypedMessage<T>` | `TypedPublisher<T>` | `TypedSubscription<T>` | `MessageCallback<T>` |
|---|---|---|---|---|
| `ControllerInput` | Yes | Yes | Yes | Yes |
| `MotorCommand` | Yes | Yes | Yes | Yes |
| `ServoCommand` | Yes | Yes | Yes | Yes |
| `SensorData` | Yes | Yes | Yes | Yes |
| `SystemStatus` | Yes | Yes | Yes | Yes |
| `AudioCommand` | Yes | Yes | Yes | Yes |
| `LEDCommand` | Yes | Yes | Yes | Yes |

That is 7 types x 4 template instantiations = 28 class instantiations.

### 6.2 Estimated Binary Cost

Each `TypedPublisher<T>` instantiation adds:
- vtable pointer + vtable (~16 bytes)
- `publish(const T&)` method code (~60-80 bytes)
- Total: ~80-100 bytes per type

Each `TypedSubscription<T>` instantiation adds:
- vtable pointer + vtable (~16 bytes)
- `handleMessage()` with `dynamic_cast` (~100-150 bytes)
- `getMessageTypeName()` (~20 bytes)
- Total: ~140-190 bytes per type

Each `TypedMessage<T>` instantiation adds:
- vtable (~16 bytes)
- `clone()`, `getTypeName()`, `getSize()` (~60 bytes)
- Total: ~80 bytes per type

**Total for 7 types**: ~2100-2600 bytes of code. This is acceptable.

### 6.3 Scaling Projection

If the project grows to 20-30 message types (plausible with animation, diagnostics, configuration messages), template bloat would reach ~6-8 KB. Still manageable for ESP32 (4 MB flash typical), but worth monitoring.

**Recommendation**: The CRTP pattern is appropriate here. If bloat becomes a concern, consider type-erased message passing for low-frequency topics (diagnostics, config) while keeping typed templates for high-frequency control messages.

---

## 7. Gaps Compared to ROS2 Concepts

| ROS2 Feature | Current Status | Priority | Notes |
|---|---|---|---|
| Node lifecycle (configure/activate/deactivate/cleanup/shutdown) | Partial -- INACTIVE/ACTIVE/PAUSED/ERROR/SHUTDOWN exist but no `configure` step | Medium | Add a `configure()` step between construction and `initialize()` for parameter loading |
| Parameters | Missing | Medium | See recommendation 3.10 |
| Services (request/reply) | Missing | Low | See recommendation 3.11 |
| Actions (long-running tasks with feedback) | Missing | Low | Useful for animation sequences; could be built on top of pub/sub |
| Timers | Missing (frequency is a node property, not a first-class timer) | Medium | See recommendation 3.9 |
| Component/composable nodes | Missing | Low | Not essential for single-process ESP32 |
| Logging levels | Present (via ESP_LOG macros) | Done | |
| Quality of Service | Declared but not enforced | High | See recommendation 3.4 |
| Launch/composition system | N/A for embedded | -- | Executor fills this role |
| Topic remapping | Missing | Low | Useful for testing (remap "controller_input" to "simulated_input") |
| Introspection (list topics, nodes, connections) | Partial (statistics exist) | Low | Add a debug dump method |

---

## 8. Memory Budget Estimate

Assuming a fully operational system with 8 nodes, 7 message types, and 10 topics:

| Component | Current Estimate | After Optimizations |
|---|---|---|
| Executor + FreeRTOS task stack | 4096 + 200 bytes | 4096 + 200 bytes |
| MessageBroker (singleton, maps, vectors) | ~2000-4000 bytes | ~500-800 bytes (flat arrays) |
| 8 Node instances (base + names + vtables) | ~800-1200 bytes | ~500-700 bytes (fixed names) |
| Publisher instances (8 publishers) | ~600-900 bytes | ~400-600 bytes |
| Subscription instances (10 subscriptions) | ~800-1200 bytes | ~500-700 bytes |
| Message pools (if implemented, 4 slots each) | 0 (heap allocated now) | ~2000-3000 bytes (static pools) |
| Template code (flash, not SRAM) | ~2500 bytes flash | ~1800 bytes (no RTTI) |
| **Total SRAM** | **~8500-11500 bytes** | **~8200-10000 bytes** |

The optimization target is not necessarily smaller total size but rather **zero runtime heap allocation** in the steady-state loop, converting dynamic allocation to predictable static allocation.

The SRAM budget of ~10 KB for the framework leaves ~310 KB for application logic, FreeRTOS kernel structures, BT/WiFi stack (~50-80 KB), and peripheral buffers. This is within acceptable bounds.

---

## 9. Summary of Recommendations by Priority

### Bugs to Fix (Immediate)

| # | Priority | Issue | Effort |
|---|---|---|---|
| 2.16 | CRITICAL | `last_process_time_` never updated -- frequency gating completely broken | Low |
| 2.17 | HIGH | `vTaskDelay` resolution mismatch -- cannot achieve 1 kHz, actual ~100 Hz | Low |
| 2.18 | HIGH | Executor flags (`should_stop_`, `is_running_`, `emergency_stop_`) not atomic | Low |
| 2.21 | MEDIUM | QoS static methods defined in both header and .cpp (ODR violation) | Low |
| 2.19 | MEDIUM | `Executor::stop()` force-deletes task unsafely | Low |
| 2.20 | MEDIUM | `addNode`/`removeNode` not thread-safe during execution | Low |

### Design Improvements

| # | Priority | Recommendation | Effort |
|---|---|---|---|
| 3.1 | CRITICAL | Memory pool for message publish | Medium |
| 3.2 | CRITICAL | Replace std::string with fixed-size identifiers | Medium |
| 3.3 | HIGH | Replace unordered_map with static flat arrays | Low |
| 3.4 | HIGH | Implement message queuing (ring buffers) | Medium |
| 3.5 | HIGH | Replace dynamic_cast/RTTI with compile-time type IDs | Low |
| 3.6 | HIGH | Use vTaskDelayUntil for periodic execution | Low |
| 3.7 | MEDIUM | Pre-compute node periods | Low |
| 3.8 | MEDIUM | Make MessageBroker injectable | Low |
| 3.9 | MEDIUM | Add timer support to nodes | Medium |
| 3.10 | LOW | Add parameter/configuration system | Medium |
| 3.11 | LOW | Add service (request/reply) pattern | Medium |

---

## 10. Conclusion

The core framework has a solid architectural foundation. The node/pub-sub/executor pattern is well-suited for the target use case. The primary risks are all related to **inappropriate use of STL containers and heap allocation in real-time paths**. Addressing the CRITICAL and HIGH items (sections 3.1-3.6) would make the framework genuinely real-time safe and suitable for extended operation on ESP32 hardware without memory degradation.

The framework is approximately 70% complete for Phase 1 goals. The remaining 30% is primarily enforcement of the constraints that are already documented in the design (QoS, determinism, zero-allocation) but not yet implemented in the code.
