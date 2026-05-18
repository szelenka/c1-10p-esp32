#pragma once

#include <cstdint>
#include <cstddef>
#include "esp_log.h"

namespace chopper::safety {

/**
 * @brief Unified 6-step emergency stop chain.
 *
 * Executes a fixed sequence of function-pointer callbacks when
 * an emergency stop is triggered. Each step is a direct function
 * call with no heap allocation and no message broker involvement.
 *
 * Steps (in order):
 *   [1] Disable all motors (MotorSafetyMonitor::disableAll)
 *   [2] Emergency shutdown all drivers (DriverManager::emergencyShutdown)
 *   [3] Notify executor (Executor::notifyEmergencyStop)
 *   [4] Log the event (ErrorLog::log)
 *   [5] Set LED status (rapid red flash)
 *   [6] Controller rumble feedback
 *
 * The chain executes top-to-bottom in <100 us total.
 * Each step is optional (null callbacks are skipped).
 */
class EmergencyStopChain {
public:
    static constexpr size_t MAX_STEPS = 6;

    /**
     * @brief Callback for a single e-stop step.
     * @param reason     Why the e-stop was triggered (const char* literal).
     * @param source_id  Identifier of the triggering component.
     * @param context    User-provided context pointer.
     */
    using StepCallback = void (*)(const char* reason, uint8_t source_id, void* context);

    EmergencyStopChain();

    /**
     * @brief Register a step callback at a specific index (0-5).
     * @param step_index  Index in the chain (0 = first executed).
     * @param name        Human-readable name for debugging.
     * @param callback    Function pointer to execute.
     * @param context     User context passed to callback.
     * @return true if registration succeeded.
     */
    bool registerStep(size_t step_index, const char* name, StepCallback callback, void* context);

    /**
     * @brief Execute the full e-stop chain.
     *
     * Runs all registered steps in order [0..MAX_STEPS-1].
     * Null callbacks are skipped. No exceptions. No heap.
     * Idempotent: calling multiple times has no additional effect
     * (triggered_ flag is set on first call).
     */
    void execute(const char* reason, uint8_t source_id);

    /// Check if the chain has been triggered.
    [[nodiscard]] bool isTriggered() const { return triggered_; }

    /// Reset the chain (allows re-triggering after recovery).
    void reset();

    /// Get the name of a registered step.
    [[nodiscard]] const char* getStepName(size_t step_index) const;

private:
    struct Step {
        const char* name = nullptr;
        StepCallback callback = nullptr;
        void* context = nullptr;
    };

    Step steps_[MAX_STEPS] = {};
    bool triggered_ = false;
};

}  // namespace chopper::safety
