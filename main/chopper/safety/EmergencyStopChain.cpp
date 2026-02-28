#include "chopper/safety/EmergencyStopChain.h"

static const char* TAG = "EStopChain";

namespace chopper {
namespace safety {

EmergencyStopChain::EmergencyStopChain()
    : triggered_(false)
{
}

bool EmergencyStopChain::registerStep(size_t step_index, const char* name,
                                       StepCallback callback, void* context) {
    if (step_index >= MAX_STEPS || !callback) {
        return false;
    }
    steps_[step_index].name = name;
    steps_[step_index].callback = callback;
    steps_[step_index].context = context;
    return true;
}

void EmergencyStopChain::execute(const char* reason, uint8_t source_id) {
    if (triggered_) {
        return;  // Already triggered, idempotent
    }
    triggered_ = true;

    ESP_LOGE(TAG, "E-STOP TRIGGERED: %s (source=%u)", reason, source_id);

    for (size_t i = 0; i < MAX_STEPS; ++i) {
        if (steps_[i].callback) {
            steps_[i].callback(reason, source_id, steps_[i].context);
        }
    }
}

void EmergencyStopChain::reset() {
    triggered_ = false;
    ESP_LOGI(TAG, "E-stop chain reset");
}

const char* EmergencyStopChain::getStepName(size_t step_index) const {
    if (step_index >= MAX_STEPS) return nullptr;
    return steps_[step_index].name;
}

} // namespace safety
} // namespace chopper
