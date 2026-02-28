#pragma once

#include "chopper/core/PublishingNode.h"
#include "chopper/core/ParameterServer.h"
#include "chopper/config/HardwareConfig.h"
#include "chopper/math/DriveMixer.h"
#include "chopper/math/MathUtil.h"
#include "chopper/messages/CommonMessages.h"

namespace chopper {
namespace nodes {

/**
 * Converts drive-controller joystick input into left/right MotorCommands.
 *
 * Reads drive parameters from ParameterServer:
 *   drive.system     — drive mode (Arcade/Curvature/Tank/ReelTwo)
 *   drive.deadband   — joystick deadband
 *   drive.max_speed  — maximum speed fraction
 *   drive.speed_boost — extra speed added in carpet mode
 *
 * Also handles thumbL double-click to toggle carpet mode (speed boost),
 * publishing an AudioCommand to confirm the toggle.
 */
class DriveNode : public core::PublishingNode {
public:
    DriveNode()
        : PublishingNode("drive")
    {}

    bool initialize() override {
        motor_pub_ = createPublisher<messages::MotorCommand>("drive/cmd");
        audio_pub_ = createPublisher<messages::AudioCommand>("audio/cmd");
        input_sub_ = createSubscription<messages::ControllerInput>(
            "controller/drive", &DriveNode::onControllerInput, this);

        auto& ps = core::ParameterServer::getInstance();
        ps.declare("drive.system", static_cast<int32_t>(config::drive_mode::ARCADE),
                   static_cast<int32_t>(0), static_cast<int32_t>(3));
        ps.declare("drive.deadband", 0.05f, 0.0f, 0.5f);
        ps.declare("drive.max_speed", 0.75f, 0.0f, 1.0f);
        ps.declare("drive.speed_boost", 0.25f, 0.0f, 1.0f);

        return motor_pub_ != nullptr && audio_pub_ != nullptr && input_sub_ != nullptr;
    }

    void process(uint64_t) override {
        // Event-driven — work happens in onControllerInput
    }

    void emergencyStop() override {
        if (motor_pub_) {
            messages::MotorCommand cmd;
            cmd.command_type = messages::MotorCommand::CommandType::EMERGENCY_STOP;
            cmd.value = 0.0f;
            cmd.motor_id = 0;
            motor_pub_->publish(cmd);
            cmd.motor_id = 1;
            motor_pub_->publish(cmd);
        }
    }

    bool isCarpetMode() const { return carpet_mode_; }

private:
    void onControllerInput(const messages::ControllerInput& input) {
        if (!motor_pub_) return;

        // Carpet mode toggle: thumbL double-click
        handleCarpetToggle(input);

        // Read parameters
        auto& ps = core::ParameterServer::getInstance();
        int32_t drive_system = config::drive_mode::ARCADE;
        float deadband = 0.05f;
        float max_speed = 0.75f;
        float speed_boost = 0.25f;
        ps.get("drive.system", drive_system);
        ps.get("drive.deadband", deadband);
        ps.get("drive.max_speed", max_speed);
        ps.get("drive.speed_boost", speed_boost);

        float effective_max = carpet_mode_ ? std::clamp(max_speed + speed_boost, 0.0f, 1.0f)
                                           : max_speed;

        float x = math::ApplyDeadband(input.axis_x_slew, deadband);
        float z = math::ApplyDeadband(input.axis_y_slew, deadband);

        math::WheelSpeeds speeds{0.0f, 0.0f};
        switch (drive_system) {
            case config::drive_mode::ARCADE:
                speeds = math::ArcadeDriveIK(x, z);
                break;
            case config::drive_mode::CURVE:
                speeds = math::CurvatureDriveIK(x, z);
                break;
            case config::drive_mode::REELTWO:
                speeds = math::ReelTwoDriveIK(x, z);
                break;
            case config::drive_mode::TANK:
                speeds = math::TankDriveIK(x, z);
                break;
        }

        // Apply max speed
        speeds.left = math::ApplySpeedLimit(speeds.left, effective_max);
        speeds.right = math::ApplySpeedLimit(speeds.right, effective_max);

        // Publish left motor (id=0)
        messages::MotorCommand left_cmd;
        left_cmd.motor_id = 0;
        left_cmd.command_type = messages::MotorCommand::CommandType::SET_SPEED;
        left_cmd.value = speeds.left;
        motor_pub_->publish(left_cmd);

        // Publish right motor (id=1)
        messages::MotorCommand right_cmd;
        right_cmd.motor_id = 1;
        right_cmd.command_type = messages::MotorCommand::CommandType::SET_SPEED;
        right_cmd.value = speeds.right;
        motor_pub_->publish(right_cmd);
    }

    void handleCarpetToggle(const messages::ControllerInput& input) {
        bool pressed = input.button_thumb_l;
        if (pressed && !last_thumb_l_) {
            // Rising edge — check for double-click
            uint64_t now = static_cast<uint64_t>(esp_timer_get_time() / 1000);
            if (now - last_thumb_l_time_ < kDoubleClickMs) {
                carpet_mode_ = !carpet_mode_;
                if (audio_pub_) {
                    messages::AudioCommand acmd;
                    acmd.command_type = messages::AudioCommand::CommandType::PLAY_TRACK;
                    acmd.track_id = carpet_mode_
                        ? static_cast<uint16_t>(config::sound_track::TADA)
                        : static_cast<uint16_t>(config::sound_track::WAH3);
                    audio_pub_->publish(acmd);
                }
            }
            last_thumb_l_time_ = now;
        }
        last_thumb_l_ = pressed;
    }

    static constexpr uint64_t kDoubleClickMs = 500;

    core::TypedPublisherPtr<messages::MotorCommand> motor_pub_;
    core::TypedPublisherPtr<messages::AudioCommand> audio_pub_;
    core::TypedSubscriptionPtr<messages::ControllerInput> input_sub_;

    bool last_thumb_l_ = false;
    uint64_t last_thumb_l_time_ = 0;
    bool carpet_mode_ = false;
};

} // namespace nodes
} // namespace chopper
