#include "chopper/core/Node.h"
#include "esp_log.h"

static const char* const TAG = "Node";

namespace chopper::core {

Node::Node(const char* name) {
    // Copy name into fixed buffer, ensure null termination
    strncpy(name_, (name != nullptr) ? name : "unnamed", sizeof(name_) - 1);
    name_[sizeof(name_) - 1] = '\0';

    ESP_LOGI(TAG, "[%s] Created node", name_);
}

bool Node::activate() {
    if (state_ != State::INACTIVE) {
        ESP_LOGW(TAG, "[%s] Cannot activate from state %d", name_, static_cast<int>(state_));
        return false;
    }

    if (!onActivate()) {
        ESP_LOGE(TAG, "[%s] Failed to activate", name_);
        setState(State::ERROR);
        return false;
    }

    setState(State::ACTIVE);
    return true;
}

bool Node::deactivate() {
    if (state_ != State::ACTIVE && state_ != State::PAUSED) {
        ESP_LOGW(TAG, "[%s] Cannot deactivate from state %d", name_, static_cast<int>(state_));
        return false;
    }

    if (!onDeactivate()) {
        ESP_LOGE(TAG, "[%s] Failed to deactivate", name_);
        setState(State::ERROR);
        return false;
    }

    setState(State::INACTIVE);
    return true;
}

void Node::setState(State new_state) {
    if (state_ != new_state) {
        static const char* const state_names[] = {"INACTIVE", "ACTIVE", "PAUSED", "ERROR", "SHUTDOWN"};
        auto idx_old = static_cast<int>(state_);
        auto idx_new = static_cast<int>(new_state);
        const char* old_str = (idx_old >= 0 && idx_old <= 4) ? state_names[idx_old] : "?";
        const char* new_str = (idx_new >= 0 && idx_new <= 4) ? state_names[idx_new] : "?";

        state_ = new_state;
        ESP_LOGI(TAG, "[%s] %s -> %s", name_, old_str, new_str);
    }
}

void Node::logError(const char* message) {
    ESP_LOGE(TAG, "[%s] %s", name_, message);
}

void Node::logWarning(const char* message) {
    ESP_LOGW(TAG, "[%s] %s", name_, message);
}

void Node::logInfo(const char* message) {
    ESP_LOGI(TAG, "[%s] %s", name_, message);
}

}  // namespace chopper::core
