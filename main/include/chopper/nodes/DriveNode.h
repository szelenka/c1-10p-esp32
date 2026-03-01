#pragma once

#include "chopper/core/PublishingNode.h"
#include "chopper/core/ParameterServer.h"
#include "chopper/config/HardwareConfig.h"
#include "chopper/math/DriveMixer.h"
#include "chopper/math/MathUtil.h"
#include "chopper/math/SlewRateLimiter.h"
#include "chopper/messages/CommonMessages.h"
#include <cmath>

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
        , slew_x_(3.0f)
        , slew_z_(3.0f)
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
        float declared_slew = 0.0f;
        if (!ps.get("ctrl.drive.slew_rate", declared_slew)) {
            ps.declare("ctrl.drive.slew_rate", 3.0f, 0.1f, 20.0f);
        }

        refreshCachedParams();
        bool listeners_ok = true;
        listeners_ok &= ps.onChange("drive.system", &DriveNode::onParameterChanged, this);
        listeners_ok &= ps.onChange("drive.deadband", &DriveNode::onParameterChanged, this);
        listeners_ok &= ps.onChange("drive.max_speed", &DriveNode::onParameterChanged, this);
        listeners_ok &= ps.onChange("drive.speed_boost", &DriveNode::onParameterChanged, this);
        listeners_ok &= ps.onChange("ctrl.drive.slew_rate", &DriveNode::onParameterChanged, this);

        return motor_pub_ != nullptr && audio_pub_ != nullptr && input_sub_ != nullptr && listeners_ok;
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

        float effective_max = carpet_mode_
            ? std::clamp(max_speed_ + speed_boost_, 0.0f, 1.0f)
            : max_speed_;

        if (std::fabs(drive_slew_rate_ - slew_rate_current_) > 0.001f) {
            slew_rate_current_ = drive_slew_rate_;
            slew_x_.Reset(slew_rate_current_, -slew_rate_current_, slew_x_.LastValue());
            slew_z_.Reset(slew_rate_current_, -slew_rate_current_, slew_z_.LastValue());
        }

        // Prefer pre-filtered slew fields when present; fall back to normalized axes.
        float axis_x = input.axis_x_slew;
        float axis_y = input.axis_y_slew;
        if (std::fabs(axis_x) < 0.0001f && std::fabs(input.axis_x_normalized) > 0.0001f) {
            axis_x = input.axis_x_normalized;
        }
        if (std::fabs(axis_y) < 0.0001f && std::fabs(input.axis_y_normalized) > 0.0001f) {
            axis_y = input.axis_y_normalized;
        }

        uint64_t now_ms = static_cast<uint64_t>(esp_timer_get_time() / 1000ULL);
        float x_limited = slew_x_.Calculate(axis_x, now_ms);
        float z_limited = slew_z_.Calculate(axis_y, now_ms);
        float x = math::ApplyDeadband(x_limited, deadband_);
        float z = math::ApplyDeadband(z_limited, deadband_);

        math::WheelSpeeds speeds{0.0f, 0.0f};
        switch (drive_system_) {
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

    static void onParameterChanged(const char*, void* context) {
        if (!context) {
            return;
        }
        auto* self = static_cast<DriveNode*>(context);
        self->refreshCachedParams();
    }

    void refreshCachedParams() {
        auto& ps = core::ParameterServer::getInstance();
        (void)ps.get("drive.system", drive_system_);
        (void)ps.get("drive.deadband", deadband_);
        (void)ps.get("drive.max_speed", max_speed_);
        (void)ps.get("drive.speed_boost", speed_boost_);
        (void)ps.get("ctrl.drive.slew_rate", drive_slew_rate_);
    }

    static constexpr uint64_t kDoubleClickMs = 500;

    core::TypedPublisherPtr<messages::MotorCommand> motor_pub_;
    core::TypedPublisherPtr<messages::AudioCommand> audio_pub_;
    core::TypedSubscriptionPtr<messages::ControllerInput> input_sub_;

    bool last_thumb_l_ = false;
    uint64_t last_thumb_l_time_ = 0;
    bool carpet_mode_ = false;
    int32_t drive_system_ = config::drive_mode::ARCADE;
    float deadband_ = 0.05f;
    float max_speed_ = 0.75f;
    float speed_boost_ = 0.25f;
    float drive_slew_rate_ = 3.0f;
    float slew_rate_current_ = 3.0f;
    math::SlewRateLimiter slew_x_;
    math::SlewRateLimiter slew_z_;
};

} // namespace nodes
} // namespace chopper
