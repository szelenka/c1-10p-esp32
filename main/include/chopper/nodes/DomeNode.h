#pragma once

#include "chopper/core/PublishingNode.h"
#include "chopper/dome/DomePosition.h"
#include "chopper/math/SlewRateLimiter.h"
#include "chopper/messages/CommonMessages.h"

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
    DomeNode(dome::DomePosition* dome_position,
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
        return motor_pub_ != nullptr && sensor_pub_ != nullptr;
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
    dome::DomePosition* dome_position_;
    float max_speed_;
    uint8_t motor_id_;
    bool inverted_;
    math::SlewRateLimiter slew_;

    core::TypedPublisherPtr<messages::MotorCommand> motor_pub_;
    core::TypedPublisherPtr<messages::SensorData> sensor_pub_;
};

} // namespace nodes
} // namespace chopper
