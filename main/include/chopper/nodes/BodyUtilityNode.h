#pragma once

#include "chopper/core/PublishingNode.h"
#include "chopper/core/ParameterServer.h"
#include "chopper/config/HardwareConfig.h"
#include "chopper/messages/CommonMessages.h"

namespace chopper {
namespace nodes {

/**
 * Controls the body utility arm via the B button.
 *
 * - B held → extend utility arm to max position
 * - B released → retract utility arm to neutral position
 *
 * Publishes ServoCommand on "servo/body/cmd".
 */
class BodyUtilityNode : public core::PublishingNode {
public:
    BodyUtilityNode()
        : PublishingNode("body_util")
    {}

    bool initialize() override {
        servo_pub_ = createPublisher<messages::ServoCommand>("servo/body/cmd");
        input_sub_ = createSubscription<messages::ControllerInput>(
            "controller/drive", &BodyUtilityNode::onControllerInput, this);

        auto& ps = core::ParameterServer::getInstance();
        ps.declare("servo.util_arm.neutral", static_cast<int32_t>(1500),
                   static_cast<int32_t>(500), static_cast<int32_t>(2500));
        ps.declare("servo.util_arm.max", static_cast<int32_t>(2500),
                   static_cast<int32_t>(500), static_cast<int32_t>(2500));

        return servo_pub_ != nullptr && input_sub_ != nullptr;
    }

    void process(uint64_t) override {}

    void emergencyStop() override {
        if (servo_pub_) {
            messages::ServoCommand cmd;
            cmd.servo_id = config::servo_channel::BODY_UTILITY_ARM;
            cmd.command_type = messages::ServoCommand::CommandType::DISABLE;
            servo_pub_->publish(cmd);
        }
    }

private:
    void onControllerInput(const messages::ControllerInput& input) {
        if (!servo_pub_) return;

        bool pressed = input.button_b;
        if (pressed != last_b_) {
            auto& ps = core::ParameterServer::getInstance();
            int32_t neutral = 1500, max_pos = 2500;
            ps.get("servo.util_arm.neutral", neutral);
            ps.get("servo.util_arm.max", max_pos);

            messages::ServoCommand cmd;
            cmd.servo_id = config::servo_channel::BODY_UTILITY_ARM;
            cmd.command_type = messages::ServoCommand::CommandType::SET_POSITION;

            if (pressed) {
                cmd.value = static_cast<float>(max_pos);
            } else {
                cmd.value = static_cast<float>(neutral);
            }
            servo_pub_->publish(cmd);
        }
        last_b_ = pressed;
    }

    core::TypedPublisherPtr<messages::ServoCommand> servo_pub_;
    core::TypedSubscriptionPtr<messages::ControllerInput> input_sub_;

    bool last_b_ = false;
};

} // namespace nodes
} // namespace chopper
