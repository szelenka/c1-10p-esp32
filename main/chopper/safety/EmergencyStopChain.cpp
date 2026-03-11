#include "chopper/safety/EmergencyStopChain.h"

static const char* const TAG = "EStopChain";

namespace chopper::safety {

EmergencyStopChain::EmergencyStopChain() = default;

bool EmergencyStopChain::registerStep(size_t step_index, const char* name, StepCallback callback, void* context) {
    if (step_index >= MAX_STEPS || (callback == nullptr)) {
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

    for (auto& step : steps_) {
        if (step.callback != nullptr) {
            step.callback(reason, source_id, step.context);
        }
    }
}

void EmergencyStopChain::reset() {
    triggered_ = false;
    ESP_LOGI(TAG, "E-stop chain reset");
}

const char* EmergencyStopChain::getStepName(size_t step_index) const {
    if (step_index >= MAX_STEPS) {
        return nullptr;
    }
    return steps_[step_index].name;
}

}  // namespace chopper::safety
