#include "chopper/safety/SafetyManager.h"

static const char* TAG = "SafetyMgr";

namespace chopper {
namespace safety {

SafetyManager::SafetyManager()
    : emergency_active_(false)
{
}

void SafetyManager::emergencyStop(const char* reason, uint8_t source_id) {
    if (emergency_active_) {
        return;  // Already in e-stop
    }
    emergency_active_ = true;

    ESP_LOGE(TAG, "EMERGENCY STOP: %s (source=%u)", reason, source_id);

    // Transition degradation to EMERGENCY_STOP
    degradation_.forceMode(DegradationMode::EMERGENCY_STOP);

    // Execute the full e-stop chain
    estop_chain_.execute(reason, source_id);

    // Log the event
    ErrorLog::getActive().log(
        ErrorLog::SAFETY_ESTOP,
        source_id,
        0,
        ErrorLog::FATAL);
}

void SafetyManager::update(uint64_t now_us) {
    if (emergency_active_) {
        return;  // Nothing to update during e-stop
    }

    // Check motor safety timeouts
    motor_safety_.checkAll(now_us);

    // If any motor timed out and we are in FULL or REDUCED mode,
    // degrade to ESSENTIAL_ONLY
    if (motor_safety_.hasAnyTimeout()) {
        DegradationMode current = degradation_.getCurrentMode();
        if (current == DegradationMode::FULL_OPERATION ||
            current == DegradationMode::REDUCED_FEATURES) {
            degradation_.requestTransition(DegradationMode::ESSENTIAL_ONLY);
        }
    }
}

bool SafetyManager::resetEmergencyStop() {
    if (!emergency_active_) {
        return false;
    }

    ESP_LOGW(TAG, "Resetting emergency stop");

    estop_chain_.reset();
    emergency_active_ = false;

    // Transition to SAFE_STOP; operator must manually upgrade from there
    degradation_.forceMode(DegradationMode::SAFE_STOP);

    return true;
}

void SafetyManager::onExecutorEStop(const char* reason, void* context) {
    if (!context) return;
    auto* self = static_cast<SafetyManager*>(context);
    self->emergencyStop(reason, 0);
}

} // namespace safety
} // namespace chopper
