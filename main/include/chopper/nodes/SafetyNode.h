#pragma once

#include "chopper/core/PublishingNode.h"
#include "chopper/safety/SafetyManager.h"
#include "chopper/messages/CommonMessages.h"

namespace chopper::nodes {

/**
 * Node that wraps the SafetyManager into the executor cycle.
 *
 * Runs at 50 Hz and:
 *   - Calls SafetyManager::update() for motor watchdog checks
 *   - Publishes SystemStatus when degradation mode changes
 */
class SafetyNode : public core::PublishingNode {
public:
    SafetyNode(safety::SafetyManager* manager)
        : PublishingNode("safety")
        , manager_(manager)
        , last_mode_((manager != nullptr) ? manager->getDegradationManager().getCurrentMode()
                                          : safety::DegradationMode::SAFE_STOP) {}

    bool initialize() override {
        if (manager_ == nullptr) {
            return false;
        }
        status_pub_ = createPublisher<messages::SystemStatus>("system/status");
        return status_pub_ != nullptr;
    }

    void process(uint64_t now) override {
        if (manager_ == nullptr) {
            return;
        }

        manager_->update(now);

        // Publish status if degradation mode changed
        auto current = manager_->getDegradationManager().getCurrentMode();
        if (current != last_mode_) {
            last_mode_ = current;
            publishStatus(current);
        }
    }

    void emergencyStop() override {
        if (manager_ != nullptr) {
            manager_->emergencyStop("Node emergency stop", 0);
        }
    }

    [[nodiscard]] double getUpdateFrequency() const override { return 50.0; }

    [[nodiscard]] safety::SafetyManager* getManager() const { return manager_; }

private:
    void publishStatus(safety::DegradationMode mode) {
        messages::SystemStatus status;
        switch (mode) {
            case safety::DegradationMode::FULL_OPERATION:
                status.system_status = messages::SystemStatus::Status::RUNNING;
                break;
            case safety::DegradationMode::REDUCED_FEATURES:
            case safety::DegradationMode::ESSENTIAL_ONLY:
                status.system_status = messages::SystemStatus::Status::WARNING;
                break;
            case safety::DegradationMode::SAFE_STOP:
                status.system_status = messages::SystemStatus::Status::ERROR;
                break;
            case safety::DegradationMode::EMERGENCY_STOP:
                status.system_status = messages::SystemStatus::Status::EMERGENCY_STOP;
                break;
        }
        strncpy(status.message, safety::degradationModeToString(mode), sizeof(status.message) - 1);
        status_pub_->publish(status);
    }

    safety::SafetyManager* manager_;
    safety::DegradationMode last_mode_;
    core::TypedPublisherPtr<messages::SystemStatus> status_pub_;
};

}  // namespace chopper::nodes
