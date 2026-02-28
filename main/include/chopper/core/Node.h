#pragma once

#include <memory>
#include <cstdint>
#include <cstring>
#include "chopper/chopper_limits.h"
#include "esp_timer.h"

namespace chopper {
namespace core {

/**
 * @brief Base class for all nodes in the Chopper framework.
 *
 * Nodes are the fundamental building blocks, inspired by ROS2 but optimized
 * for single-core ESP32 execution. Each node has a single, well-defined purpose.
 */
class Node {
public:
    enum class State {
        INACTIVE,   ///< Created but not started
        ACTIVE,     ///< Running normally
        PAUSED,     ///< Paused but can resume
        ERROR,      ///< Error state
        SHUTDOWN    ///< Shutting down
    };

    /**
     * @brief Constructor
     * @param name Unique name for this node (string literal or static storage)
     */
    explicit Node(const char* name);

    virtual ~Node() = default;

    /// Get node name (null-terminated, fixed-size buffer).
    const char* getName() const { return name_; }

    /// Get current node state.
    State getState() const { return state_; }

    /// Initialize the node. Called once before activate().
    virtual bool initialize() = 0;

    /// Activate the node (INACTIVE -> ACTIVE).
    virtual bool activate();

    /// Deactivate the node (ACTIVE -> INACTIVE).
    virtual bool deactivate();

    /// Main processing function. Called periodically when ACTIVE.
    /// Must be real-time safe (no dynamic allocation, no blocking).
    virtual void process(uint64_t now) = 0;

    /// Handle safety shutdown.
    virtual void emergencyStop() = 0;

    /// Desired update frequency in Hz. 0 means event-driven only.
    virtual double getUpdateFrequency() const { return 0.0; }

    /// Get last process execution time in microseconds.
    uint64_t getLastProcessTime() const { return last_process_time_; }

    /// Set last process time (called by Executor after process()).
    void setLastProcessTime(uint64_t time_us) { last_process_time_ = time_us; }

    /// Get maximum allowed execution time in microseconds.
    uint64_t getMaxExecutionTime() const { return max_execution_time_us_; }

    /// Set maximum allowed execution time.
    void setMaxExecutionTime(uint64_t max_time_us) { max_execution_time_us_ = max_time_us; }

protected:
    void setState(State new_state);
    virtual bool onActivate() { return true; }
    virtual bool onDeactivate() { return true; }

    void logError(const char* message);
    void logWarning(const char* message);
    void logInfo(const char* message);

private:
    char name_[limits::MAX_NODE_NAME_LEN];
    State state_;
    uint64_t last_process_time_;
    uint64_t max_execution_time_us_;

    Node(const Node&) = delete;
    Node& operator=(const Node&) = delete;
};

using NodePtr = std::shared_ptr<Node>;

} // namespace core
} // namespace chopper
