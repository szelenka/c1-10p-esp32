#pragma once

#include <cstdint>
#include <cstddef>
#include "chopper/safety/ErrorLog.h"
#include "chopper/safety/MotorSafetyMonitor.h"
#include "chopper/safety/DegradationManager.h"
#include "chopper/safety/EmergencyStopChain.h"
#include "esp_log.h"

namespace chopper::safety {

/**
 * @brief Central safety coordinator.
 *
 * The SafetyManager owns the ErrorLog, DegradationManager,
 * MotorSafetyMonitor, and EmergencyStopChain. It provides the
 * single entry point for emergency stops from any trigger source.
 *
 * Integration with the Executor is via the emergencyStop callback:
 *   executor.setEmergencyStopCallback(&SafetyManager::onExecutorEStop, &manager);
 */
class SafetyManager {
public:
    SafetyManager();

    /// Get the ErrorLog (ring buffer).
    ErrorLog& getErrorLog() { return ErrorLog::getActive(); }

    /// Get the motor safety monitor.
    MotorSafetyMonitor& getMotorSafetyMonitor() { return motor_safety_; }

    /// Get the degradation manager.
    DegradationManager& getDegradationManager() { return degradation_; }

    /// Get the emergency stop chain.
    EmergencyStopChain& getEmergencyStopChain() { return estop_chain_; }

    /**
     * @brief Trigger an emergency stop from any source.
     *
     * This is the single unified e-stop entry point. It:
     *   1. Transitions degradation to EMERGENCY_STOP.
     *   2. Executes the full e-stop chain.
     *   3. Logs the event.
     *
     * @param reason     Human-readable reason (const char* literal).
     * @param source_id  Identifier of the triggering component.
     */
    void emergencyStop(const char* reason, uint8_t source_id);

    /**
     * @brief Run periodic safety checks. Call from executor main loop.
     *
     * Checks motor timeouts, evaluates degradation triggers,
     * and transitions modes as needed.
     *
     * @param now_us  Current timestamp in microseconds.
     */
    void update(uint64_t now_us);

    /// Check if an emergency stop is active.
    bool isEmergencyStopped() const { return emergency_active_; }

    /**
     * @brief Reset from emergency stop state.
     *
     * Resets the e-stop chain and transitions to SAFE_STOP mode.
     * The system must then be manually upgraded to FULL_OPERATION.
     *
     * @return true if reset was successful.
     */
    bool resetEmergencyStop();

    /**
     * @brief Static callback suitable for Executor::setEmergencyStopCallback.
     *
     * context must be a pointer to SafetyManager.
     */
    static void onExecutorEStop(const char* reason, void* context);

private:
    MotorSafetyMonitor motor_safety_;
    DegradationManager degradation_;
    EmergencyStopChain estop_chain_;
    bool emergency_active_ = false;
};

}  // namespace chopper::safety
