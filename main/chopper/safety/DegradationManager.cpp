#include "chopper/safety/DegradationManager.h"
#include "esp_timer.h"

static const char* TAG = "DegradMgr";

namespace chopper {
namespace safety {

DegradationManager::DegradationManager()
    : current_mode_(DegradationMode::SAFE_STOP)
    , transition_count_(0)
    , last_transition_time_us_(0)
    , callback_count_(0)
{
}

bool DegradationManager::requestTransition(DegradationMode new_mode) {
    // Only allow transitions toward more-degraded modes
    if (static_cast<uint8_t>(new_mode) <= static_cast<uint8_t>(current_mode_)) {
        return false;
    }

    DegradationMode old_mode = current_mode_;
    executeTransition(old_mode, new_mode);
    return true;
}

bool DegradationManager::requestUpgrade(DegradationMode target_mode) {
    // Cannot upgrade from EMERGENCY_STOP (requires full reset)
    if (current_mode_ == DegradationMode::EMERGENCY_STOP) {
        return false;
    }

    // Upgrade must be to a less-degraded mode
    if (static_cast<uint8_t>(target_mode) >= static_cast<uint8_t>(current_mode_)) {
        return false;
    }

    // Only allow upgrade by one step at a time
    uint8_t current_val = static_cast<uint8_t>(current_mode_);
    uint8_t target_val = static_cast<uint8_t>(target_mode);
    if (current_val - target_val != 1) {
        return false;
    }

    DegradationMode old_mode = current_mode_;
    executeTransition(old_mode, target_mode);
    return true;
}

void DegradationManager::forceMode(DegradationMode mode) {
    DegradationMode old_mode = current_mode_;
    if (old_mode != mode) {
        executeTransition(old_mode, mode);
    }
}

bool DegradationManager::registerTransitionCallback(TransitionCallback cb, void* context) {
    if (!cb || callback_count_ >= MAX_TRANSITION_CALLBACKS) {
        return false;
    }
    callbacks_[callback_count_].callback = cb;
    callbacks_[callback_count_].context = context;
    callback_count_++;
    return true;
}

void DegradationManager::executeTransition(DegradationMode old_mode, DegradationMode new_mode) {
    current_mode_ = new_mode;
    transition_count_++;
    last_transition_time_us_ = static_cast<uint64_t>(esp_timer_get_time());

    ESP_LOGW(TAG, "Mode transition: %s -> %s",
             degradationModeToString(old_mode),
             degradationModeToString(new_mode));

    // Log the transition
    ErrorLog::getActive().log(
        ErrorLog::DEGRADATION,
        static_cast<uint8_t>(old_mode),
        static_cast<uint32_t>(new_mode),
        ErrorLog::INFO);

    // Fire all registered callbacks
    for (size_t i = 0; i < callback_count_; ++i) {
        if (callbacks_[i].callback) {
            callbacks_[i].callback(old_mode, new_mode, callbacks_[i].context);
        }
    }
}

} // namespace safety
} // namespace chopper
