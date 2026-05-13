#pragma once

#include "chopper/core/PublishingNode.h"
#include "chopper/hal/IMotorDriver.h"
#include "chopper/messages/CommonMessages.h"

namespace chopper::nodes {

/**
 * Bridge node: subscribes to MotorCommand messages on a given topic
 * and forwards them to an IMotorDriver instance.
 *
 * One node per physical motor (e.g., "drive_left", "drive_right", "dome").
 * The node filters commands by motor_id so multiple motors can share a topic.
 */
class MotorBridgeNode : public core::PublishingNode {
public:
    /**
     * @param name      Node name (e.g., "drive_left_bridge").
     * @param topic     Topic to subscribe to (e.g., "drive/cmd").
     * @param driver    Motor driver to forward commands to.
     * @param motor_id  Only process commands matching this motor_id.
     */
    MotorBridgeNode(const char* name, const char* topic, hal::IMotorDriver* driver, uint8_t motor_id)
        : PublishingNode(name), topic_(topic), driver_(driver), motor_id_(motor_id) {}

    bool initialize() override {
        if (driver_ == nullptr) {
            return false;
        }
        sub_ = createSubscription<messages::MotorCommand>(topic_, &MotorBridgeNode::onCommand, this);
        return sub_ != nullptr;
    }

    void process(uint64_t) override {
        // Event-driven only — no periodic work needed
    }

    void emergencyStop() override {
        if (driver_ != nullptr) {
            driver_->stop();
        }
    }

    [[nodiscard]] hal::IMotorDriver* getDriver() const { return driver_; }
    [[nodiscard]] uint8_t getMotorId() const { return motor_id_; }

private:
    void onCommand(const messages::MotorCommand& cmd) {
        if ((driver_ == nullptr) || cmd.motor_id != motor_id_) {
            return;
        }

        switch (cmd.command_type) {  // NOLINT(bugprone-branch-clone)
            case messages::MotorCommand::CommandType::SET_SPEED:
                driver_->set(cmd.value);
                break;
            case messages::MotorCommand::CommandType::STOP:
                driver_->stop();
                break;
            case messages::MotorCommand::CommandType::EMERGENCY_STOP:
                driver_->stop();
                break;
            default:
                break;
        }
    }

    const char* topic_;
    hal::IMotorDriver* driver_;
    uint8_t motor_id_;
    core::TypedSubscriptionPtr<messages::MotorCommand> sub_;
};

}  // namespace chopper::nodes
