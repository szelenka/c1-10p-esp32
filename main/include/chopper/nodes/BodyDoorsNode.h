#pragma once

#include "chopper/core/PublishingNode.h"
#include "chopper/core/ParameterServer.h"
#include "chopper/config/HardwareConfig.h"
#include "chopper/messages/CommonMessages.h"

namespace chopper::nodes {

/**
 * Toggles individual body doors from canonical controller intents.
 *
 * Publishes timed ServoCommand requests on "servo/body/move".
 */
class BodyDoorsNode : public core::PublishingNode {
public:
    BodyDoorsNode() : PublishingNode("body_doors") {}

    bool initialize() override {
        servo_pub_ = createPublisher<messages::ServoCommand>("servo/body/move");
        input_sub_ =
            createSubscription<messages::ControllerInput>("controller/drive", &BodyDoorsNode::onControllerInput, this);

        auto& ps = core::ParameterServer::getInstance();
        refreshCachedParams();
        bool listeners_ok = true;
        listeners_ok &= ps.onChange("servo.bdoor_r.min", &BodyDoorsNode::onParameterChanged, this);
        listeners_ok &= ps.onChange("servo.bdoor_r.neutral", &BodyDoorsNode::onParameterChanged, this);
        listeners_ok &= ps.onChange("servo.bdoor_l.max", &BodyDoorsNode::onParameterChanged, this);
        listeners_ok &= ps.onChange("servo.bdoor_l.neutral", &BodyDoorsNode::onParameterChanged, this);

        return servo_pub_ != nullptr && input_sub_ != nullptr && listeners_ok;
    }

    void process(uint64_t) override {}

    void emergencyStop() override {
        if (servo_pub_) {
            messages::ServoCommand cmd;
            cmd.command_type = messages::ServoCommand::CommandType::DISABLE;
            cmd.servo_id = config::servo_channel::BODY_DOOR_RIGHT;
            servo_pub_->publish(cmd);
            cmd.servo_id = config::servo_channel::BODY_DOOR_LEFT;
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

        const bool left_pressed = input.has_intents ? input.intent_body_left_door_toggle : input.button_l1;
        const bool right_pressed = input.has_intents ? input.intent_body_right_door_toggle : input.button_r1;

        if (left_pressed && !last_left_) {
            toggleLeftDoor();
        }
        if (right_pressed && !last_right_) {
            toggleRightDoor();
        }

        last_left_ = left_pressed;
        last_right_ = right_pressed;
    }

    void toggleLeftDoor() {
        messages::ServoCommand cmd;
        cmd.servo_id = config::servo_channel::BODY_DOOR_LEFT;
        cmd.command_type = messages::ServoCommand::CommandType::SET_POSITION;
        cmd.duration_ms = kDoorMoveMs;
        cmd.has_start_value = true;

        if (left_door_open_) {
            cmd.start_value = static_cast<float>(ldoor_max_);
            cmd.value = static_cast<float>(ldoor_neutral_);
            left_door_open_ = false;
        } else {
            cmd.start_value = static_cast<float>(ldoor_neutral_);
            cmd.value = static_cast<float>(ldoor_max_);
            left_door_open_ = true;
        }
        servo_pub_->publish(cmd);
    }

    void toggleRightDoor() {
        messages::ServoCommand cmd;
        cmd.servo_id = config::servo_channel::BODY_DOOR_RIGHT;
        cmd.command_type = messages::ServoCommand::CommandType::SET_POSITION;
        cmd.duration_ms = kDoorMoveMs;
        cmd.has_start_value = true;

        if (right_door_open_) {
            cmd.start_value = static_cast<float>(rdoor_min_);
            cmd.value = static_cast<float>(rdoor_neutral_);
            right_door_open_ = false;
        } else {
            cmd.start_value = static_cast<float>(rdoor_neutral_);
            cmd.value = static_cast<float>(rdoor_min_);
            right_door_open_ = true;
        }
        servo_pub_->publish(cmd);
    }

    static void onParameterChanged(const char*, void* context) {
        if (context == nullptr) {
            return;
        }
        auto* self = static_cast<BodyDoorsNode*>(context);
        self->refreshCachedParams();
    }

    void refreshCachedParams() {
        auto& ps = core::ParameterServer::getInstance();
        (void)ps.get("servo.bdoor_r.min", rdoor_min_);
        (void)ps.get("servo.bdoor_r.neutral", rdoor_neutral_);
        (void)ps.get("servo.bdoor_l.max", ldoor_max_);
        (void)ps.get("servo.bdoor_l.neutral", ldoor_neutral_);
    }

    core::TypedPublisherPtr<messages::ServoCommand> servo_pub_;
    core::TypedSubscriptionPtr<messages::ControllerInput> input_sub_;

    static constexpr uint16_t kDoorMoveMs = 800;

    bool last_left_ = false;
    bool last_right_ = false;
    bool left_door_open_ = false;
    bool right_door_open_ = false;
    int32_t rdoor_min_ = 992;
    int32_t rdoor_neutral_ = 1920;
    int32_t ldoor_max_ = 1696;
    int32_t ldoor_neutral_ = 1030;
};

}  // namespace chopper::nodes
