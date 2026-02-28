#pragma once

#include "chopper/core/PublishingNode.h"
#include "chopper/core/ParameterServer.h"
#include "chopper/config/HardwareConfig.h"
#include "chopper/messages/CommonMessages.h"

namespace chopper {
namespace nodes {

/**
 * Toggles dome doors open/closed on miscSelect button press.
 *
 * Both doors toggle together:
 *   - Right dome door: min ↔ max
 *   - Left dome door: neutral ↔ max
 *
 * Publishes ServoCommand on "servo/dome/cmd".
 */
class DomeArmsNode : public core::PublishingNode {
public:
    DomeArmsNode()
        : PublishingNode("dome_arms")
    {}

    bool initialize() override {
        servo_pub_ = createPublisher<messages::ServoCommand>("servo/dome/cmd");
        input_sub_ = createSubscription<messages::ControllerInput>(
            "controller/drive", &DomeArmsNode::onControllerInput, this);

        auto& ps = core::ParameterServer::getInstance();
        ps.declare("servo.ddoor_r.min", static_cast<int32_t>(500),
                   static_cast<int32_t>(500), static_cast<int32_t>(2500));
        ps.declare("servo.ddoor_r.max", static_cast<int32_t>(2500),
                   static_cast<int32_t>(500), static_cast<int32_t>(2500));
        ps.declare("servo.ddoor_l.neutral", static_cast<int32_t>(1500),
                   static_cast<int32_t>(500), static_cast<int32_t>(2500));
        ps.declare("servo.ddoor_l.max", static_cast<int32_t>(2500),
                   static_cast<int32_t>(500), static_cast<int32_t>(2500));

        return servo_pub_ != nullptr && input_sub_ != nullptr;
    }

    void process(uint64_t) override {}

    void emergencyStop() override {
        if (servo_pub_) {
            messages::ServoCommand cmd;
            cmd.command_type = messages::ServoCommand::CommandType::DISABLE;
            cmd.servo_id = config::servo_channel::DOME_DOOR_RIGHT;
            servo_pub_->publish(cmd);
            cmd.servo_id = config::servo_channel::DOME_DOOR_LEFT;
            servo_pub_->publish(cmd);
        }
    }

    bool isRightDoorOpen() const { return right_door_open_; }
    bool isLeftDoorOpen() const { return left_door_open_; }

private:
    void onControllerInput(const messages::ControllerInput& input) {
        if (!servo_pub_) return;

        bool pressed = input.misc_select;
        if (pressed && !last_misc_select_) {
            toggleDoors();
        }
        last_misc_select_ = pressed;
    }

    void toggleDoors() {
        auto& ps = core::ParameterServer::getInstance();
        int32_t rdoor_min = 500, rdoor_max = 2500;
        int32_t ldoor_neutral = 1500, ldoor_max = 2500;
        ps.get("servo.ddoor_r.min", rdoor_min);
        ps.get("servo.ddoor_r.max", rdoor_max);
        ps.get("servo.ddoor_l.neutral", ldoor_neutral);
        ps.get("servo.ddoor_l.max", ldoor_max);

        // Right door
        {
            messages::ServoCommand cmd;
            cmd.servo_id = config::servo_channel::DOME_DOOR_RIGHT;
            cmd.command_type = messages::ServoCommand::CommandType::SET_POSITION;
            if (right_door_open_) {
                cmd.value = static_cast<float>(rdoor_min);
                right_door_open_ = false;
            } else {
                cmd.value = static_cast<float>(rdoor_max);
                right_door_open_ = true;
            }
            servo_pub_->publish(cmd);
        }

        // Left door
        {
            messages::ServoCommand cmd;
            cmd.servo_id = config::servo_channel::DOME_DOOR_LEFT;
            cmd.command_type = messages::ServoCommand::CommandType::SET_POSITION;
            if (left_door_open_) {
                cmd.value = static_cast<float>(ldoor_neutral);
                left_door_open_ = false;
            } else {
                cmd.value = static_cast<float>(ldoor_max);
                left_door_open_ = true;
            }
            servo_pub_->publish(cmd);
        }
    }

    core::TypedPublisherPtr<messages::ServoCommand> servo_pub_;
    core::TypedSubscriptionPtr<messages::ControllerInput> input_sub_;

    bool right_door_open_ = true;
    bool left_door_open_ = true;
    bool last_misc_select_ = false;
};

} // namespace nodes
} // namespace chopper
