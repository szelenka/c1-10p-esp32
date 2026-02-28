#pragma once

#include "chopper/core/PublishingNode.h"
#include "chopper/hal/IServoController.h"
#include "chopper/messages/CommonMessages.h"

namespace chopper {
namespace nodes {

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
    ServoBridgeNode(const char* name, const char* topic,
                    hal::IServoController* controller)
        : PublishingNode(name)
        , topic_(topic)
        , controller_(controller)
    {}

    bool initialize() override {
        if (!controller_) return false;
        sub_ = createSubscription<messages::ServoCommand>(
            topic_, &ServoBridgeNode::onCommand, this);
        return sub_ != nullptr;
    }

    void process(uint64_t) override {
        // Event-driven only
    }

    void emergencyStop() override {
        if (controller_) {
            controller_->disableAll();
        }
    }

    hal::IServoController* getController() const { return controller_; }

private:
    void onCommand(const messages::ServoCommand& cmd) {
        if (!controller_) return;
        if (cmd.servo_id >= controller_->getChannelCount()) return;

        switch (cmd.command_type) {
            case messages::ServoCommand::CommandType::SET_POSITION: {
                // Convert normalized 0.0-1.0 to pulse width (500-2500us)
                uint16_t pulse = static_cast<uint16_t>(
                    500.0f + cmd.value * 2000.0f);
                controller_->setPosition(cmd.servo_id, pulse);
                break;
            }
            case messages::ServoCommand::CommandType::SET_SPEED:
                controller_->setSpeed(cmd.servo_id,
                    static_cast<uint16_t>(cmd.value));
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
    core::TypedSubscriptionPtr<messages::ServoCommand> sub_;
};

} // namespace nodes
} // namespace chopper
