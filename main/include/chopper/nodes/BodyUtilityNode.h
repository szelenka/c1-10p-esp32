#pragma once

#include "chopper/core/PublishingNode.h"
#include "chopper/core/ParameterServer.h"
#include "chopper/config/HardwareConfig.h"
#include "chopper/messages/CommonMessages.h"

namespace chopper::nodes {

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
    BodyUtilityNode() : PublishingNode("body_util") {}

    bool initialize() override {
        servo_pub_ = createPublisher<messages::ServoCommand>("servo/body/cmd");
        input_sub_ = createSubscription<messages::ControllerInput>("controller/drive",
                                                                   &BodyUtilityNode::onControllerInput, this);

        auto& ps = core::ParameterServer::getInstance();

        // Params are declared in DefaultParameters.h (single source of truth).
        // Just read current values and register for change notifications.
        refreshCachedParams();
        bool listeners_ok = true;
        listeners_ok &= ps.onChange("servo.util_arm.neutral", &BodyUtilityNode::onParameterChanged, this);
        listeners_ok &= ps.onChange("servo.util_arm.max", &BodyUtilityNode::onParameterChanged, this);

        return servo_pub_ != nullptr && input_sub_ != nullptr && listeners_ok;
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
        if (!servo_pub_) {
            return;
        }

        const bool pressed = input.has_intents ? input.intent_body_utility_toggle : input.button_b;
        if (pressed != last_b_) {
            messages::ServoCommand cmd;
            cmd.servo_id = config::servo_channel::BODY_UTILITY_ARM;
            cmd.command_type = messages::ServoCommand::CommandType::SET_POSITION;

            if (pressed) {
                cmd.value = static_cast<float>(max_pos_);
            } else {
                cmd.value = static_cast<float>(neutral_);
            }
            servo_pub_->publish(cmd);
        }
        last_b_ = pressed;
    }

    static void onParameterChanged(const char*, void* context) {
        if (context == nullptr) {
            return;
        }
        auto* self = static_cast<BodyUtilityNode*>(context);
        self->refreshCachedParams();
    }

    void refreshCachedParams() {
        auto& ps = core::ParameterServer::getInstance();
        (void)ps.get("servo.util_arm.neutral", neutral_);
        (void)ps.get("servo.util_arm.max", max_pos_);
    }

    core::TypedPublisherPtr<messages::ServoCommand> servo_pub_;
    core::TypedSubscriptionPtr<messages::ControllerInput> input_sub_;

    bool last_b_ = false;
    int32_t neutral_ = 1500;
    int32_t max_pos_ = 2500;
};

}  // namespace chopper::nodes
