#pragma once

#include "chopper/core/PublishingNode.h"
#include "chopper/core/ParameterServer.h"
#include "chopper/config/HardwareConfig.h"
#include "chopper/messages/CommonMessages.h"

namespace chopper {
namespace nodes {

/**
 * Manages periscope lift and spin from the drive controller.
 *
 * - X button: toggle periscope lift (up/down)
 * - A button: spin left one step (double-click: full left)
 * - Y button: spin right one step (double-click: full right)
 *
 * Spin only works when periscope is down (retracted/stowed).
 * Publishes ServoCommand on "servo/dome/cmd".
 */
class PeriscopeNode : public core::PublishingNode {
public:
    PeriscopeNode()
        : PublishingNode("periscope")
    {}

    bool initialize() override {
        servo_pub_ = createPublisher<messages::ServoCommand>("servo/dome/cmd");
        input_sub_ = createSubscription<messages::ControllerInput>(
            "controller/drive", &PeriscopeNode::onControllerInput, this);

        auto& ps = core::ParameterServer::getInstance();
        ps.declare("servo.peri_lift.min", static_cast<int32_t>(500),
                   static_cast<int32_t>(500), static_cast<int32_t>(2500));
        ps.declare("servo.peri_lift.max", static_cast<int32_t>(2500),
                   static_cast<int32_t>(500), static_cast<int32_t>(2500));
        ps.declare("servo.peri_spin.min", static_cast<int32_t>(500),
                   static_cast<int32_t>(500), static_cast<int32_t>(2500));
        ps.declare("servo.peri_spin.max", static_cast<int32_t>(2500),
                   static_cast<int32_t>(500), static_cast<int32_t>(2500));
        ps.declare("servo.peri_spin.neutral", static_cast<int32_t>(1500),
                   static_cast<int32_t>(500), static_cast<int32_t>(2500));

        return servo_pub_ != nullptr && input_sub_ != nullptr;
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

    bool isPeriscopeDown() const { return periscope_down_; }
    int8_t getPeriscopeLocation() const { return periscope_location_; }

private:
    void onControllerInput(const messages::ControllerInput& input) {
        if (!servo_pub_) return;

        uint64_t now = static_cast<uint64_t>(esp_timer_get_time() / 1000);

        handleLift(input, now);
        handleSpin(input, now);
    }

    void handleLift(const messages::ControllerInput& input, uint64_t now) {
        bool pressed = input.button_x;
        if (pressed && !last_x_) {
            auto& ps = core::ParameterServer::getInstance();
            int32_t lift_min = 500, lift_max = 2500;
            ps.get("servo.peri_lift.min", lift_min);
            ps.get("servo.peri_lift.max", lift_max);

            messages::ServoCommand cmd;
            cmd.servo_id = config::servo_channel::DOME_PERISCOPE_LIFT;
            cmd.command_type = messages::ServoCommand::CommandType::SET_POSITION;

            if (periscope_down_) {
                cmd.value = static_cast<float>(lift_max);
                periscope_down_ = false;
            } else {
                cmd.value = static_cast<float>(lift_min);
                periscope_down_ = true;
            }
            servo_pub_->publish(cmd);
        }
        last_x_ = pressed;
    }

    void handleSpin(const messages::ControllerInput& input, uint64_t now) {
        // Spin only works when periscope is stowed (down)
        if (!periscope_down_) {
            last_a_ = input.button_a;
            last_y_ = input.button_y;
            return;
        }

        auto& ps = core::ParameterServer::getInstance();
        int32_t spin_min = 500, spin_max = 2500, spin_neutral = 1500;
        ps.get("servo.peri_spin.min", spin_min);
        ps.get("servo.peri_spin.max", spin_max);
        ps.get("servo.peri_spin.neutral", spin_neutral);

        // A button: spin left
        if (input.button_a && !last_a_) {
            bool double_click = (now - last_a_time_ < kDoubleClickMs);
            last_a_time_ = now;

            messages::ServoCommand cmd;
            cmd.servo_id = config::servo_channel::DOME_PERISCOPE_SPIN;
            cmd.command_type = messages::ServoCommand::CommandType::SET_POSITION;

            if (double_click) {
                // Full left
                cmd.value = static_cast<float>(spin_max);
                periscope_location_ = -1;
            } else {
                // One step left
                if (periscope_location_ == 0) {
                    cmd.value = static_cast<float>(spin_max);
                    periscope_location_ = -1;
                } else if (periscope_location_ == 1) {
                    cmd.value = static_cast<float>(spin_neutral);
                    periscope_location_ = 0;
                } else {
                    // Already full left — no-op
                    last_a_ = input.button_a;
                    return;
                }
            }
            servo_pub_->publish(cmd);
        }
        last_a_ = input.button_a;

        // Y button: spin right
        if (input.button_y && !last_y_) {
            bool double_click = (now - last_y_time_ < kDoubleClickMs);
            last_y_time_ = now;

            messages::ServoCommand cmd;
            cmd.servo_id = config::servo_channel::DOME_PERISCOPE_SPIN;
            cmd.command_type = messages::ServoCommand::CommandType::SET_POSITION;

            if (double_click) {
                // Full right
                cmd.value = static_cast<float>(spin_min);
                periscope_location_ = 1;
            } else {
                // One step right
                if (periscope_location_ == 0) {
                    cmd.value = static_cast<float>(spin_min);
                    periscope_location_ = 1;
                } else if (periscope_location_ == -1) {
                    cmd.value = static_cast<float>(spin_neutral);
                    periscope_location_ = 0;
                } else {
                    // Already full right — no-op
                    last_y_ = input.button_y;
                    return;
                }
            }
            servo_pub_->publish(cmd);
        }
        last_y_ = input.button_y;
    }

    static constexpr uint64_t kDoubleClickMs = 500;

    core::TypedPublisherPtr<messages::ServoCommand> servo_pub_;
    core::TypedSubscriptionPtr<messages::ControllerInput> input_sub_;

    bool periscope_down_ = true;
    int8_t periscope_location_ = 0;  // -1=left, 0=center, 1=right

    bool last_x_ = false;
    bool last_a_ = false;
    bool last_y_ = false;
    uint64_t last_a_time_ = 0;
    uint64_t last_y_time_ = 0;
};

} // namespace nodes
} // namespace chopper
