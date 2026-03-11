#pragma once

#include <cstdint>
#include <cstddef>
#include "chopper/safety/ErrorLog.h"
#include "esp_log.h"

namespace chopper::safety {

/**
 * @brief Degradation mode for the system.
 *
 * Five operational modes forming a linear degradation chain.
 * Higher numeric value = more degraded.
 */
enum class DegradationMode : uint8_t {
    FULL_OPERATION = 0,    ///< All nodes active, all features enabled
    REDUCED_FEATURES = 1,  ///< LED, audio, telemetry disabled
    ESSENTIAL_ONLY = 2,    ///< Only drive (reduced speed), controller, safety
    SAFE_STOP = 3,         ///< All actuators stopped, waiting for reconnect
    EMERGENCY_STOP = 4,    ///< All outputs disabled, requires reset
};

/// Convert DegradationMode to a human-readable string.
inline const char* degradationModeToString(DegradationMode mode) {
    switch (mode) {
        case DegradationMode::FULL_OPERATION:
            return "FULL";
        case DegradationMode::REDUCED_FEATURES:
            return "REDUCED";
        case DegradationMode::ESSENTIAL_ONLY:
            return "ESSENTIAL";
        case DegradationMode::SAFE_STOP:
            return "SAFE_STOP";
        case DegradationMode::EMERGENCY_STOP:
            return "ESTOP";
        default:
            return "UNKNOWN";
    }
}

/**
 * @brief Manages graceful degradation through 5 operational modes.
 *
 * The DegradationManager is a state machine that tracks the current
 * operational mode and manages transitions. Each transition can trigger
 * a user-supplied callback for node enable/disable logic.
 */
class DegradationManager {
public:
    static constexpr size_t MAX_TRANSITION_CALLBACKS = 8;

    /**
     * @brief Callback invoked on mode transitions.
     * @param old_mode  The mode being left.
     * @param new_mode  The mode being entered.
     * @param context   User-provided context pointer.
     */
    using TransitionCallback = void (*)(DegradationMode old_mode, DegradationMode new_mode, void* context);

    DegradationManager();

    /// Get the current degradation mode.
    [[nodiscard]] DegradationMode getCurrentMode() const { return current_mode_; }

    /**
     * @brief Request a transition to a new mode.
     *
     * Transitions are only allowed in the degradation direction
     * (toward more degraded) unless requestUpgrade() is used.
     * E.g., FULL -> REDUCED is allowed, REDUCED -> FULL is not.
     *
     * @return true if the transition was executed.
     */
    bool requestTransition(DegradationMode new_mode);

    /**
     * @brief Request an upgrade to a less-degraded mode.
     *
     * Upgrades are only allowed by one step at a time.
     * EMERGENCY_STOP cannot be upgraded from (requires reset).
     *
     * @return true if the upgrade was executed.
     */
    bool requestUpgrade(DegradationMode target_mode);

    /**
     * @brief Force a transition to any mode (no direction check).
     *
     * Used for system reset after recovery.
     */
    void forceMode(DegradationMode mode);

    /**
     * @brief Register a callback for mode transitions.
     * @return true if registration succeeded (slot available).
     */
    bool registerTransitionCallback(TransitionCallback cb, void* context);

    /// Get the number of transitions since boot.
    [[nodiscard]] uint32_t getTransitionCount() const { return transition_count_; }

    /// Get the timestamp of the last transition.
    [[nodiscard]] uint64_t getLastTransitionTime() const { return last_transition_time_us_; }

private:
    void executeTransition(DegradationMode old_mode, DegradationMode new_mode);

    DegradationMode current_mode_ = DegradationMode::SAFE_STOP;
    uint32_t transition_count_ = 0;
    uint64_t last_transition_time_us_ = 0;

    struct CallbackEntry {
        TransitionCallback callback = nullptr;
        void* context = nullptr;
    };
    CallbackEntry callbacks_[MAX_TRANSITION_CALLBACKS] = {};
    size_t callback_count_ = 0;
};

}  // namespace chopper::safety
