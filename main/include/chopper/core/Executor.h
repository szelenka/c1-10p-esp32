#pragma once

#include "Node.h"
#include "MessageBroker.h"
#include "chopper/chopper_limits.h"
#include <vector>
#include <memory>
#include <atomic>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"

namespace chopper::core {

/**
 * @brief Real-time executor for managing node execution.
 *
 * Responsibilities:
 * - Scheduling node execution based on update frequencies
 * - Enforcing real-time constraints
 * - Managing emergency stops and safety systems
 * - Processing messages between nodes
 */
class Executor {
public:
    struct Config {
        uint32_t loop_frequency_hz = 100;         ///< Main loop frequency (100 Hz default, matches FreeRTOS tick)
        uint32_t max_loop_time_us = 9000;         ///< Maximum allowed loop time (90% of 10ms period)
        bool loop_timeout_triggers_estop = true;  ///< If false, loop overruns warn but do not E-stop
        uint32_t watchdog_timeout_ms = 2000;      ///< Watchdog timeout
        uint32_t emergency_stop_timeout_ms = 100;
        bool node_timeout_triggers_estop =
            true;  ///< If false, timeouted node is emergency-stopped but executor keeps running
        bool enable_statistics = true;
        size_t max_nodes = limits::MAX_NODES;
        int8_t executor_task_core = -1;  ///< -1: scheduler default, otherwise core id
    };

    struct Statistics {
        uint64_t loop_count = 0;
        uint64_t max_loop_time_us = 0;
        uint64_t avg_loop_time_us = 0;
        uint64_t missed_deadlines = 0;
        uint64_t emergency_stops = 0;
        uint64_t watchdog_resets = 0;
        uint32_t active_nodes = 0;
        uint32_t total_nodes = 0;
    };

    /// Emergency stop callback: reason (const char*), context (void*)
    using EmergencyStopCallback = void (*)(const char* reason, void* context);

    Executor();
    explicit Executor(const Config& config);
    ~Executor();

    /// @note Must be called before start().
    bool addNode(const NodePtr& node);

    /// @note Must be called before start().
    bool removeNode(const NodePtr& node);

    bool initializeNodes();
    bool activateNodes();
    bool deactivateNodes();

    bool start();
    bool stop();

    /// Trigger emergency stop. Uses const char* — no heap allocation.
    void emergencyStop(const char* reason);
    /// Request cooperative soft-stop: invokes emergencyStop() on active nodes
    /// but does not latch executor emergency-stop or stop the loop.
    void softStop(const char* reason);
    /// Clear cooperative soft-stop and resume normal node scheduling.
    void clearSoftStop(const char* reason);
    bool isSoftStopped() const { return soft_stop_.load(); }

    bool isRunning() const { return is_running_.load(); }
    bool isEmergencyStop() const { return emergency_stop_.load(); }

    Statistics getStatistics() const;
    void resetStatistics();

    void setEmergencyStopCallback(EmergencyStopCallback callback, void* context = nullptr);

    void feedWatchdog();

private:
    void executionLoop();
    static void taskWrapper(void* parameter);
    uint64_t processNodes(uint64_t now);
    void checkSafety(uint64_t execution_time_us);
    void updateStatistics(uint64_t execution_time_us);

    Config config_;
    std::vector<NodePtr> nodes_;  // NOLINT(heap) init-time only, not on hot path
    MessageBroker& message_broker_;

    TaskHandle_t task_handle_;
    std::atomic<bool> is_running_;
    std::atomic<bool> should_stop_;
    std::atomic<bool> emergency_stop_;
    std::atomic<bool> soft_stop_;

    uint64_t last_loop_time_;
    uint64_t last_watchdog_feed_;
    EmergencyStopCallback emergency_callback_;
    void* emergency_callback_context_;

    mutable std::mutex stats_mutex_;
    Statistics stats_;
    uint64_t loop_time_accumulator_;

public:
    Executor(const Executor&) = delete;
    Executor& operator=(const Executor&) = delete;
};

}  // namespace chopper::core
