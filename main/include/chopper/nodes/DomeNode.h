#pragma once

#include "chopper/core/PublishingNode.h"
#include "chopper/core/ParameterServer.h"
#include "chopper/dome/DomePosition.h"
#include "chopper/math/MathUtil.h"
#include "chopper/math/SlewRateLimiter.h"
#include "chopper/messages/CommonMessages.h"
#include "esp_timer.h"

#include <algorithm>

namespace chopper {
namespace nodes {

/**
 * Node that manages dome spin and publishes dome position data.
 *
 * Subscribes to ControllerInput on two topics (drive + dome) for
 * the R2-trigger dome spin control, and publishes MotorCommand
 * for the dome spin motor plus SensorData for the dome angle.
 *
 * The dome spin logic: if both or neither R2 buttons are pressed → stop.
 * If only the drive R2 → spin positive. Only dome R2 → spin negative.
 */
class DomeNode : public core::PublishingNode {
public:
    /**
     * @param dome_position  Dome position tracker (externally owned).
     * @param max_speed      Maximum dome spin speed (0..1).
     * @param slew_rate      Slew rate for smooth dome spin ramping.
     * @param motor_id       Motor ID for the dome spin motor.
     * @param inverted       Invert dome spin direction.
     */
    explicit DomeNode(dome::DomePosition* dome_position = nullptr,
             float max_speed = 0.5f,
             float slew_rate = 1.0f,
             uint8_t motor_id = 0,
             bool inverted = false)
        : PublishingNode("dome")
        , dome_position_(dome_position)
        , max_speed_(max_speed)
        , motor_id_(motor_id)
        , inverted_(inverted)
        , slew_(slew_rate)
    {}

    bool initialize() override {
        motor_pub_ = createPublisher<messages::MotorCommand>("dome/motor/cmd");
        sensor_pub_ = createPublisher<messages::SensorData>("dome/position");
        input_sub_ = createSubscription<messages::ControllerInput>(
            "controller/dome", &DomeNode::onControllerInput, this);

        auto& ps = core::ParameterServer::getInstance();
        ps.declare("dome.max_speed", max_speed_, 0.0f, 1.0f);
        ps.declare("dome.deadband", 0.05f, 0.0f, 0.5f);
        ps.declare("dome.motor_inverted", inverted_);
        ps.declare("dome.spin_slew_rate", 2.0f, 0.1f, 20.0f);

        return motor_pub_ != nullptr && sensor_pub_ != nullptr && input_sub_ != nullptr;
    }

    void process(uint64_t now) override {
        // Publish dome position if available
        if (dome_position_ && dome_position_->ready()) {
            messages::SensorData sd;
            sd.sensor_id = 0;
            sd.sensor_type = messages::SensorData::SensorType::POSITION;
            sd.value = static_cast<float>(dome_position_->getDomePosition());
            sd.is_valid = true;
            sensor_pub_->publish(sd);
        }
    }

    void emergencyStop() override {
        // Stop the dome motor
        if (motor_pub_) {
            messages::MotorCommand cmd;
            cmd.motor_id = motor_id_;
            cmd.command_type = messages::MotorCommand::CommandType::EMERGENCY_STOP;
            cmd.value = 0.0f;
            motor_pub_->publish(cmd);
        }
    }

    double getUpdateFrequency() const override { return 20.0; }

    /**
     * Set dome spin from two triggers (drive R2 + dome R2).
     * Call from controller input handler.
     * @param now_ms  Current time in ms for slew rate limiter.
     */
    void setDomeSpin(bool drive_r2, bool dome_r2, uint64_t now_ms) {
        float target = 0.0f;
        if (drive_r2 && !dome_r2) {
            target = inverted_ ? -max_speed_ : max_speed_;
        } else if (!drive_r2 && dome_r2) {
            target = inverted_ ? max_speed_ : -max_speed_;
        }
        // else both or neither → 0

        float speed = slew_.Calculate(target, now_ms);

        if (motor_pub_) {
            messages::MotorCommand cmd;
            cmd.motor_id = motor_id_;
            cmd.command_type = messages::MotorCommand::CommandType::SET_SPEED;
            cmd.value = speed;
            motor_pub_->publish(cmd);
        }
    }

    dome::DomePosition* getDomePosition() const { return dome_position_; }

private:
    void onControllerInput(const messages::ControllerInput& input) {
        if (!motor_pub_) {
            return;
        }

        auto& ps = core::ParameterServer::getInstance();
        float max_speed = max_speed_;
        float deadband = 0.05f;
        bool inverted = inverted_;
        float slew_rate = 2.0f;
        (void)ps.get("dome.max_speed", max_speed);
        (void)ps.get("dome.deadband", deadband);
        (void)ps.get("dome.motor_inverted", inverted);
        (void)ps.get("dome.spin_slew_rate", slew_rate);

        // Dome spin is controlled by DOME stick X.
        float target = math::ApplyDeadband(input.axis_x_normalized, deadband);
        target = std::clamp(target, -1.0f, 1.0f) * std::clamp(max_speed, 0.0f, 1.0f);
        if (inverted) {
            target = -target;
        }

        uint64_t now_ms = static_cast<uint64_t>(esp_timer_get_time() / 1000ULL);
        if (std::fabs(slew_rate - slew_rate_current_) > 0.001f) {
            slew_rate_current_ = slew_rate;
            slew_.Reset(slew_rate_current_, -slew_rate_current_, slew_.LastValue());
        }
        float speed = slew_.Calculate(target, now_ms);

        messages::MotorCommand cmd;
        cmd.motor_id = motor_id_;
        cmd.command_type = messages::MotorCommand::CommandType::SET_SPEED;
        cmd.value = speed;
        motor_pub_->publish(cmd);
    }

    dome::DomePosition* dome_position_;
    float max_speed_;
    uint8_t motor_id_;
    bool inverted_;
    float slew_rate_current_ = 1.0f;
    math::SlewRateLimiter slew_;

    core::TypedPublisherPtr<messages::MotorCommand> motor_pub_;
    core::TypedPublisherPtr<messages::SensorData> sensor_pub_;
    core::TypedSubscriptionPtr<messages::ControllerInput> input_sub_;
};

} // namespace nodes
} // namespace chopper
