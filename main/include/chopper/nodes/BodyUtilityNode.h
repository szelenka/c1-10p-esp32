#pragma once

#include "chopper/core/PublishingNode.h"
#include "chopper/core/ParameterServer.h"
#include "chopper/config/HardwareConfig.h"
#include "chopper/messages/CommonMessages.h"

namespace chopper::nodes {

/**
 * Controls the body utility arm via the B button / canonical intent.
 *
 * - Legacy raw B held → extend utility arm to max position
 * - Legacy raw B released → retract utility arm to neutral position
 * - Canonical intent press → toggle open/closed
 *
 * Publishes timed ServoCommand requests on "servo/body/move".
 */
class BodyUtilityNode : public core::PublishingNode {
public:
    BodyUtilityNode() : PublishingNode("body_util") {}

    bool initialize() override {
        servo_pub_ = createPublisher<messages::ServoCommand>("servo/body/move");
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
        if (input.has_intents) {
            if (pressed && !last_b_) {
                toggleUtilityArm();
            }
        } else if (pressed != last_b_) {
            publishUtilityMove(pressed ? static_cast<float>(neutral_) : static_cast<float>(max_pos_),
                               pressed ? static_cast<float>(max_pos_) : static_cast<float>(neutral_));
            utility_open_ = pressed;
        }
        last_b_ = pressed;
    }

    void toggleUtilityArm() {
        if (utility_open_) {
            publishUtilityMove(static_cast<float>(max_pos_), static_cast<float>(neutral_));
            utility_open_ = false;
        } else {
            publishUtilityMove(static_cast<float>(neutral_), static_cast<float>(max_pos_));
            utility_open_ = true;
        }
    }

    void publishUtilityMove(float start, float value) {
        messages::ServoCommand cmd;
        cmd.servo_id = config::servo_channel::BODY_UTILITY_ARM;
        cmd.command_type = messages::ServoCommand::CommandType::SET_POSITION;
        cmd.duration_ms = kUtilityArmMoveMs;
        cmd.has_start_value = true;
        cmd.start_value = start;
        cmd.value = value;
        servo_pub_->publish(cmd);
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

    static constexpr uint16_t kUtilityArmMoveMs = 800;

    bool last_b_ = false;
    bool utility_open_ = false;
    int32_t neutral_ = 1500;
    int32_t max_pos_ = 2500;
};

}  // namespace chopper::nodes
