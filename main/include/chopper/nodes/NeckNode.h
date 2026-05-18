#pragma once

#include "chopper/core/PublishingNode.h"
#include "chopper/dome/RSSMechanism.h"
#include "chopper/messages/CommonMessages.h"
#include "esp_timer.h"

namespace chopper::nodes {

/**
 * Node that runs the 3-RSS inverse kinematics for the robot's neck.
 *
 * Subscribes to ControllerInput (dome controller) and converts
 * joystick X/Y into three servo PWM commands published as ServoCommand.
 *
 * Also handles:
 *   - Enable/disable toggle via button_thumb_l double-click
 *   - Height increment/decrement via L1/R1 buttons
 */
class NeckNode : public core::PublishingNode {
public:
    /**
     * @param mechanism  Pointer to the RSSMechanism (owned externally).
     * @param input_topic   Topic for ControllerInput (e.g., "controller/dome").
     * @param servo_topic   Topic to publish ServoCommand (e.g., "servo/body/cmd").
     * @param servo_id_a    Servo channel for leg A.
     * @param servo_id_b    Servo channel for leg B.
     * @param servo_id_c    Servo channel for leg C.
     */
    NeckNode(dome::RSSMechanism* mechanism, const char* input_topic, const char* servo_topic, uint8_t servo_id_a = 0,
             uint8_t servo_id_b = 1, uint8_t servo_id_c = 2)
        : PublishingNode("neck"), mechanism_(mechanism), input_topic_(input_topic), servo_topic_(servo_topic) {
        servo_ids_[0] = servo_id_a;
        servo_ids_[1] = servo_id_b;
        servo_ids_[2] = servo_id_c;
    }

    bool initialize() override {
        if (mechanism_ == nullptr) {
            return false;
        }
        servo_pub_ = createPublisher<messages::ServoCommand>(servo_topic_);
        input_sub_ = createSubscription<messages::ControllerInput>(input_topic_, &NeckNode::onControllerInput, this);
        return servo_pub_ != nullptr && input_sub_ != nullptr;
    }

    void process(uint64_t) override {
        // Event-driven — work happens in onControllerInput
    }

    void emergencyStop() override {
        // Disable all three neck servos
        if (servo_pub_ != nullptr) {
            for (const uint8_t servo_id : servo_ids_) {
                messages::ServoCommand cmd;
                cmd.servo_id = servo_id;
                cmd.command_type = messages::ServoCommand::CommandType::DISABLE;
                servo_pub_->publish(cmd);
            }
        }
    }

private:
    void onControllerInput(const messages::ControllerInput& input) {
        if (mechanism_ == nullptr || servo_pub_ == nullptr) {
            return;
        }

        const uint64_t now_ms =
            time_override_enabled_ ? last_time_ms_ : static_cast<uint64_t>(esp_timer_get_time() / 1000ULL);
        last_time_ms_ = now_ms;

        // Handle enable/disable toggle
        const bool neck_toggle = input.has_intents ? input.intent_neck_toggle : input.button_thumb_l;
        if (neck_toggle && !last_thumb_l_) {
            mechanism_->setEnabled(!mechanism_->isEnabled(), now_ms);
            if (!mechanism_->isEnabled()) {
                disableServos();
                last_thumb_l_ = neck_toggle;
                return;
            }
        }
        last_thumb_l_ = neck_toggle;

        // Height adjust
        const bool height_down = input.has_intents ? input.intent_neck_height_down : input.button_l1;
        const bool height_up = input.has_intents ? input.intent_neck_height_up : input.button_r1;
        if (height_down) {
            mechanism_->decrementHeight(1.0f);
        }
        if (height_up) {
            mechanism_->incrementHeight(1.0f);
        }

        // Run IK: joystick slew values → PWM
        auto pwm = mechanism_->getLegPWMFromJoystick(input.axis_x_slew, input.axis_y_slew, now_ms);

        // Publish servo commands for each leg
        for (size_t i = 0; i < pwm.size(); ++i) {
            if (pwm[i] == 0) {
                continue;  // disabled → skip
            }
            messages::ServoCommand cmd;
            cmd.servo_id = servo_ids_[i];
            cmd.command_type = messages::ServoCommand::CommandType::SET_POSITION;
            // Publish raw PWM in the value field for direct IK output
            cmd.value = static_cast<float>(pwm[i]);
            servo_pub_->publish(cmd);
        }
    }

    dome::RSSMechanism* mechanism_;
    const char* input_topic_;
    const char* servo_topic_;
    uint8_t servo_ids_[3] = {0, 1, 2};

    core::TypedPublisherPtr<messages::ServoCommand> servo_pub_;
    core::TypedSubscriptionPtr<messages::ControllerInput> input_sub_;

    bool last_thumb_l_ = false;
    uint64_t last_time_ms_ = 0;
    bool time_override_enabled_ = false;

    void disableServos() {
        for (const uint8_t servo_id : servo_ids_) {
            messages::ServoCommand cmd;
            cmd.servo_id = servo_id;
            cmd.command_type = messages::ServoCommand::CommandType::DISABLE;
            servo_pub_->publish(cmd);
        }
    }

public:
    /// Allow tests / executor to inject time
    void setTime(uint64_t ms) {
        last_time_ms_ = ms;
        time_override_enabled_ = true;
    }
};

}  // namespace chopper::nodes
