#pragma once

#include "chopper/core/PublishingNode.h"
#include "chopper/hal/IServoController.h"
#include "chopper/messages/CommonMessages.h"
#include "chopper/safety/DegradationManager.h"

namespace chopper::nodes {

/**
 * Bridge node: subscribes to ServoCommand messages on a given topic
 * and forwards them to an IServoController instance.
 *
 * One node per servo controller board (e.g., "body_servos", "dome_servos").
 */
class ServoBridgeNode : public core::PublishingNode {
public:
    /**
     * @param name      Node name (e.g., "body_servo_bridge").
     * @param topic     Topic to subscribe to (e.g., "servo/body/cmd").
     * @param controller  Servo controller to forward commands to.
     */
    ServoBridgeNode(const char* name, const char* topic, hal::IServoController* controller,
                    safety::DegradationManager* degradation_mgr = nullptr)
        : PublishingNode(name), topic_(topic), controller_(controller), degradation_mgr_(degradation_mgr) {}

    bool initialize() override {
        if (controller_ == nullptr) {
            return false;
        }
        sub_ = createSubscription<messages::ServoCommand>(topic_, &ServoBridgeNode::onCommand, this);
        return sub_ != nullptr;
    }

    void process(uint64_t) override {
        // Event-driven only
    }

    void emergencyStop() override {
        if (controller_ != nullptr) {
            controller_->disableAll();
        }
    }

    [[nodiscard]] hal::IServoController* getController() const { return controller_; }

private:
    void onCommand(const messages::ServoCommand& cmd) {
        if (controller_ == nullptr) {
            return;
        }
        if (cmd.servo_id >= controller_->getChannelCount()) {
            return;
        }

        if (degradation_mgr_ != nullptr && degradation_mgr_->getCurrentMode() >= safety::DegradationMode::SAFE_STOP) {
            return;
        }

        switch (cmd.command_type) {
            case messages::ServoCommand::CommandType::SET_POSITION: {
                // Value is already in pulse-width microseconds
                controller_->enable(cmd.servo_id);
                controller_->setPosition(cmd.servo_id, static_cast<uint16_t>(cmd.value));
                break;
            }
            case messages::ServoCommand::CommandType::SET_SPEED:
                controller_->setSpeed(cmd.servo_id, static_cast<uint16_t>(cmd.value));
                break;
            case messages::ServoCommand::CommandType::ENABLE:
                controller_->enable(cmd.servo_id);
                break;
            case messages::ServoCommand::CommandType::DISABLE:
                controller_->disable(cmd.servo_id);
                break;
        }
    }

    const char* topic_;
    hal::IServoController* controller_;
    safety::DegradationManager* degradation_mgr_;
    core::TypedSubscriptionPtr<messages::ServoCommand> sub_;
};

}  // namespace chopper::nodes
