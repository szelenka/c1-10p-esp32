#pragma once

#include "chopper/core/PublishingNode.h"
#include "chopper/core/ParameterServer.h"
#include "chopper/config/HardwareConfig.h"
#include "chopper/messages/CommonMessages.h"

namespace chopper::nodes {

/**
 * Manages periscope lift and spin from the drive controller.
 *
 * - Lift from canonical intents: PERISCOPE_UP / PERISCOPE_DOWN.
 *   Legacy fallback: X toggles up/down.
 * - Spin from canonical intents: PERISCOPE_SPIN_LEFT / PERISCOPE_SPIN_RIGHT.
 *   Legacy fallback: A = left, Y = right.
 *
 * Spin only works when periscope is down (retracted/stowed).
 * Publishes ServoCommand on "servo/dome/cmd".
 */
class PeriscopeNode : public core::PublishingNode {
public:
    PeriscopeNode() : PublishingNode("periscope") {}

    bool initialize() override {
        servo_pub_ = createPublisher<messages::ServoCommand>("servo/dome/cmd");
        input_sub_ =
            createSubscription<messages::ControllerInput>("controller/drive", &PeriscopeNode::onControllerInput, this);

        auto& ps = core::ParameterServer::getInstance();
        ps.declare("servo.peri_lift.min", static_cast<int32_t>(500), static_cast<int32_t>(500),
                   static_cast<int32_t>(2500));
        ps.declare("servo.peri_lift.max", static_cast<int32_t>(2500), static_cast<int32_t>(500),
                   static_cast<int32_t>(2500));
        ps.declare("servo.peri_spin.min", static_cast<int32_t>(500), static_cast<int32_t>(500),
                   static_cast<int32_t>(2500));
        ps.declare("servo.peri_spin.max", static_cast<int32_t>(2500), static_cast<int32_t>(500),
                   static_cast<int32_t>(2500));
        ps.declare("servo.peri_spin.neutral", static_cast<int32_t>(1500), static_cast<int32_t>(500),
                   static_cast<int32_t>(2500));

        refreshCachedParams();
        bool listeners_ok = true;
        listeners_ok &= ps.onChange("servo.peri_lift.min", &PeriscopeNode::onParameterChanged, this);
        listeners_ok &= ps.onChange("servo.peri_lift.max", &PeriscopeNode::onParameterChanged, this);
        listeners_ok &= ps.onChange("servo.peri_spin.min", &PeriscopeNode::onParameterChanged, this);
        listeners_ok &= ps.onChange("servo.peri_spin.max", &PeriscopeNode::onParameterChanged, this);
        listeners_ok &= ps.onChange("servo.peri_spin.neutral", &PeriscopeNode::onParameterChanged, this);

        return servo_pub_ != nullptr && input_sub_ != nullptr && listeners_ok;
    }

    void process(uint64_t) override {}

    void emergencyStop() override {
        if (servo_pub_) {
            messages::ServoCommand cmd;
            cmd.command_type = messages::ServoCommand::CommandType::DISABLE;
            cmd.servo_id = config::servo_channel::DOME_PERISCOPE_LIFT;
            servo_pub_->publish(cmd);
            cmd.servo_id = config::servo_channel::DOME_PERISCOPE_SPIN;
            servo_pub_->publish(cmd);
        }
    }

    [[nodiscard]] bool isPeriscopeDown() const { return periscope_down_; }
    [[nodiscard]] int8_t getPeriscopeLocation() const { return periscope_location_; }

private:
    void onControllerInput(const messages::ControllerInput& input) {
        if (!servo_pub_) {
            return;
        }

        auto now = static_cast<uint64_t>(esp_timer_get_time() / 1000);

        handleLift(input, now);
        handleSpin(input, now);
    }

    void handleLift(const messages::ControllerInput& input, uint64_t now) {
        (void)now;
        const bool using_intents = input.has_intents;
        const bool up_pressed = using_intents ? input.intent_periscope_up : false;
        const bool down_pressed = using_intents ? input.intent_periscope_down : false;
        const bool toggle_pressed = using_intents ? false : input.button_x;

        if (using_intents) {
            if (up_pressed && !last_lift_up_ && periscope_down_) {
                moveLiftTo(static_cast<float>(lift_max_));
                periscope_down_ = false;
            }
            if (down_pressed && !last_lift_down_ && !periscope_down_) {
                moveLiftTo(static_cast<float>(lift_min_));
                periscope_down_ = true;
            }
        } else {
            if (toggle_pressed && !last_lift_toggle_) {
                if (periscope_down_) {
                    moveLiftTo(static_cast<float>(lift_max_));
                    periscope_down_ = false;
                } else {
                    moveLiftTo(static_cast<float>(lift_min_));
                    periscope_down_ = true;
                }
            }
        }
        last_lift_up_ = up_pressed;
        last_lift_down_ = down_pressed;
        last_lift_toggle_ = toggle_pressed;
    }

    void handleSpin(const messages::ControllerInput& input, uint64_t now) {
        const bool spin_left_pressed = input.has_intents ? input.intent_periscope_spin_left : input.button_a;
        const bool spin_right_pressed = input.has_intents ? input.intent_periscope_spin_right : input.button_y;

        // Spin only works when periscope is stowed (down)
        if (!periscope_down_) {
            last_spin_left_ = spin_left_pressed;
            last_spin_right_ = spin_right_pressed;
            return;
        }

        // Spin left
        if (spin_left_pressed && !last_spin_left_) {
            bool double_click = (now - last_a_time_ < kDoubleClickMs);
            last_a_time_ = now;

            messages::ServoCommand cmd;
            cmd.servo_id = config::servo_channel::DOME_PERISCOPE_SPIN;
            cmd.command_type = messages::ServoCommand::CommandType::SET_POSITION;

            if (double_click) {
                // Full left
                cmd.value = static_cast<float>(spin_max_);
                periscope_location_ = -1;
            } else {
                // One step left
                if (periscope_location_ == 0) {
                    cmd.value = static_cast<float>(spin_max_);
                    periscope_location_ = -1;
                } else if (periscope_location_ == 1) {
                    cmd.value = static_cast<float>(spin_neutral_);
                    periscope_location_ = 0;
                } else {
                    // Already full left — no-op
                    last_spin_left_ = spin_left_pressed;
                    return;
                }
            }
            servo_pub_->publish(cmd);
        }
        last_spin_left_ = spin_left_pressed;

        // Spin right
        if (spin_right_pressed && !last_spin_right_) {
            bool double_click = (now - last_y_time_ < kDoubleClickMs);
            last_y_time_ = now;

            messages::ServoCommand cmd;
            cmd.servo_id = config::servo_channel::DOME_PERISCOPE_SPIN;
            cmd.command_type = messages::ServoCommand::CommandType::SET_POSITION;

            if (double_click) {
                // Full right
                cmd.value = static_cast<float>(spin_min_);
                periscope_location_ = 1;
            } else {
                // One step right
                if (periscope_location_ == 0) {
                    cmd.value = static_cast<float>(spin_min_);
                    periscope_location_ = 1;
                } else if (periscope_location_ == -1) {
                    cmd.value = static_cast<float>(spin_neutral_);
                    periscope_location_ = 0;
                } else {
                    // Already full right — no-op
                    last_spin_right_ = spin_right_pressed;
                    return;
                }
            }
            servo_pub_->publish(cmd);
        }
        last_spin_right_ = spin_right_pressed;
    }

    void moveLiftTo(float pwm_us) {
        messages::ServoCommand cmd;
        cmd.servo_id = config::servo_channel::DOME_PERISCOPE_LIFT;
        cmd.command_type = messages::ServoCommand::CommandType::SET_POSITION;
        cmd.value = pwm_us;
        servo_pub_->publish(cmd);
    }

    static void onParameterChanged(const char*, void* context) {
        if (context == nullptr) {
            return;
        }
        auto* self = static_cast<PeriscopeNode*>(context);
        self->refreshCachedParams();
    }

    void refreshCachedParams() {
        auto& ps = core::ParameterServer::getInstance();
        (void)ps.get("servo.peri_lift.min", lift_min_);
        (void)ps.get("servo.peri_lift.max", lift_max_);
        (void)ps.get("servo.peri_spin.min", spin_min_);
        (void)ps.get("servo.peri_spin.max", spin_max_);
        (void)ps.get("servo.peri_spin.neutral", spin_neutral_);
    }

    static constexpr uint64_t kDoubleClickMs = 500;

    core::TypedPublisherPtr<messages::ServoCommand> servo_pub_;
    core::TypedSubscriptionPtr<messages::ControllerInput> input_sub_;

    bool periscope_down_ = true;
    int8_t periscope_location_ = 0;  // -1=left, 0=center, 1=right

    bool last_lift_toggle_ = false;
    bool last_lift_up_ = false;
    bool last_lift_down_ = false;
    bool last_spin_left_ = false;
    bool last_spin_right_ = false;
    uint64_t last_a_time_ = 0;
    uint64_t last_y_time_ = 0;
    int32_t lift_min_ = 500;
    int32_t lift_max_ = 2500;
    int32_t spin_min_ = 500;
    int32_t spin_max_ = 2500;
    int32_t spin_neutral_ = 1500;
};

}  // namespace chopper::nodes
