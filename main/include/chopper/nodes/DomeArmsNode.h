#pragma once

#include "chopper/core/PublishingNode.h"
#include "chopper/core/ParameterServer.h"
#include "chopper/config/HardwareConfig.h"
#include "chopper/messages/CommonMessages.h"

namespace chopper::nodes {

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
    DomeArmsNode() : PublishingNode("dome_arms") {}

    bool initialize() override {
        servo_pub_ = createPublisher<messages::ServoCommand>("servo/dome/cmd");
        input_sub_ =
            createSubscription<messages::ControllerInput>("controller/drive", &DomeArmsNode::onControllerInput, this);

        auto& ps = core::ParameterServer::getInstance();
        ps.declare("servo.ddoor_r.min", static_cast<int32_t>(500), static_cast<int32_t>(500),
                   static_cast<int32_t>(2500));
        ps.declare("servo.ddoor_r.max", static_cast<int32_t>(2500), static_cast<int32_t>(500),
                   static_cast<int32_t>(2500));
        ps.declare("servo.ddoor_l.neutral", static_cast<int32_t>(1500), static_cast<int32_t>(500),
                   static_cast<int32_t>(2500));
        ps.declare("servo.ddoor_l.max", static_cast<int32_t>(2500), static_cast<int32_t>(500),
                   static_cast<int32_t>(2500));

        refreshCachedParams();
        bool listeners_ok = true;
        listeners_ok &= ps.onChange("servo.ddoor_r.min", &DomeArmsNode::onParameterChanged, this);
        listeners_ok &= ps.onChange("servo.ddoor_r.max", &DomeArmsNode::onParameterChanged, this);
        listeners_ok &= ps.onChange("servo.ddoor_l.neutral", &DomeArmsNode::onParameterChanged, this);
        listeners_ok &= ps.onChange("servo.ddoor_l.max", &DomeArmsNode::onParameterChanged, this);

        return servo_pub_ != nullptr && input_sub_ != nullptr && listeners_ok;
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

    [[nodiscard]] bool isRightDoorOpen() const { return right_door_open_; }
    [[nodiscard]] bool isLeftDoorOpen() const { return left_door_open_; }

private:
    void onControllerInput(const messages::ControllerInput& input) {
        if (!servo_pub_) {
            return;
        }

        const bool pressed = input.has_intents ? input.intent_dome_doors_toggle : input.misc_select;
        if (pressed && !last_misc_select_) {
            toggleDoors();
        }
        last_misc_select_ = pressed;
    }

    void toggleDoors() {
        // Right door
        {
            messages::ServoCommand cmd;
            cmd.servo_id = config::servo_channel::DOME_DOOR_RIGHT;
            cmd.command_type = messages::ServoCommand::CommandType::SET_POSITION;
            if (right_door_open_) {
                cmd.value = static_cast<float>(rdoor_min_);
                right_door_open_ = false;
            } else {
                cmd.value = static_cast<float>(rdoor_max_);
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
                cmd.value = static_cast<float>(ldoor_neutral_);
                left_door_open_ = false;
            } else {
                cmd.value = static_cast<float>(ldoor_max_);
                left_door_open_ = true;
            }
            servo_pub_->publish(cmd);
        }
    }

    static void onParameterChanged(const char*, void* context) {
        if (context == nullptr) {
            return;
        }
        auto* self = static_cast<DomeArmsNode*>(context);
        self->refreshCachedParams();
    }

    void refreshCachedParams() {
        auto& ps = core::ParameterServer::getInstance();
        (void)ps.get("servo.ddoor_r.min", rdoor_min_);
        (void)ps.get("servo.ddoor_r.max", rdoor_max_);
        (void)ps.get("servo.ddoor_l.neutral", ldoor_neutral_);
        (void)ps.get("servo.ddoor_l.max", ldoor_max_);
    }

    core::TypedPublisherPtr<messages::ServoCommand> servo_pub_;
    core::TypedSubscriptionPtr<messages::ControllerInput> input_sub_;

    bool right_door_open_ = true;
    bool left_door_open_ = true;
    bool last_misc_select_ = false;
    int32_t rdoor_min_ = 500;
    int32_t rdoor_max_ = 2500;
    int32_t ldoor_neutral_ = 1500;
    int32_t ldoor_max_ = 2500;
};

}  // namespace chopper::nodes
