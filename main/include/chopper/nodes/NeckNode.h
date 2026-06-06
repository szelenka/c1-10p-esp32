#pragma once

#include "chopper/core/PublishingNode.h"
#include "chopper/dome/RSSMechanism.h"
#include "chopper/messages/CommonMessages.h"
#include "esp_timer.h"
#include <algorithm>
#include <cmath>

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

        if (handleNeckToggle(input, now_ms)) {
            return;
        }

        // Height adjust
        const bool height_down = input.has_intents ? input.intent_neck_height_down : input.button_l1;
        const bool height_up = input.has_intents ? input.intent_neck_height_up : input.button_r1;
        if (height_down) {
            mechanism_->decrementHeight(1.0f);
        }
        if (height_up) {
            mechanism_->incrementHeight(1.0f);
        }

        // c034 configured the dome controller output range to +/-rss.limit_normal before RSS IK.
        const float x = scaleRssAxis(input.axis_x_slew, mechanism_->getLimitNormalVector());
        const float y = scaleRssAxis(input.axis_y_slew, mechanism_->getLimitNormalVector());
        auto pwm = mechanism_->getLegPWMFromJoystick(x, y, now_ms);

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
    bool thumb_l_click_pending_ = false;
    uint64_t last_thumb_l_time_ = 0;
    uint64_t last_time_ms_ = 0;
    bool time_override_enabled_ = false;

    bool handleNeckToggle(const messages::ControllerInput& input, uint64_t now_ms) {
        const bool pressed = input.has_intents ? input.intent_neck_toggle : input.button_thumb_l;
        bool handled_disable = false;

        if (pressed && !last_thumb_l_) {
            const bool within_window =
                thumb_l_click_pending_ && now_ms >= last_thumb_l_time_ && now_ms - last_thumb_l_time_ <= kDoubleClickMs;
            if (within_window) {
                thumb_l_click_pending_ = false;
                last_thumb_l_time_ = 0;
                mechanism_->setEnabled(!mechanism_->isEnabled(), now_ms);
                if (!mechanism_->isEnabled()) {
                    disableServos();
                    handled_disable = true;
                }
            } else {
                thumb_l_click_pending_ = true;
                last_thumb_l_time_ = now_ms;
            }
        }

        last_thumb_l_ = pressed;
        return handled_disable;
    }

    void disableServos() {
        for (const uint8_t servo_id : servo_ids_) {
            messages::ServoCommand cmd;
            cmd.servo_id = servo_id;
            cmd.command_type = messages::ServoCommand::CommandType::DISABLE;
            servo_pub_->publish(cmd);
        }
    }

    [[nodiscard]] static float scaleRssAxis(float axis, float limit_normal) {
        if (!std::isfinite(axis) || !std::isfinite(limit_normal) || limit_normal <= 0.0f) {
            return 0.0f;
        }
        return std::clamp(axis, -1.0f, 1.0f) * std::clamp(limit_normal, 0.0f, 1.0f);
    }

    static constexpr uint64_t kDoubleClickMs = 500;

public:
    /// Allow tests / executor to inject time
    void setTime(uint64_t ms) {
        last_time_ms_ = ms;
        time_override_enabled_ = true;
    }
};

}  // namespace chopper::nodes
