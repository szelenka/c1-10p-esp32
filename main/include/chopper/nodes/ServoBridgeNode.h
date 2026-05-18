#pragma once

#include "chopper/chopper_limits.h"
#include "chopper/core/PublishingNode.h"
#include "chopper/hal/IServoController.h"
#include "chopper/messages/CommonMessages.h"
#include "chopper/safety/DegradationManager.h"

#include <cmath>
#include <cstdint>

namespace chopper::nodes {

/**
 * Bridge node: subscribes to ServoCommand messages on a given topic
 * and forwards them to an IServoController instance.
 *
 * One node per servo controller board (e.g., "body_servos", "dome_servos").
 */
class ServoBridgeNode : public core::PublishingNode {
public:
    /**
     * @param name      Node name (e.g., "body_servo_bridge").
     * @param topic     Topic to subscribe to (e.g., "servo/body/cmd").
     * @param controller  Servo controller to forward commands to.
     */
    ServoBridgeNode(const char* name, const char* topic, hal::IServoController* controller,
                    safety::DegradationManager* degradation_mgr = nullptr)
        : PublishingNode(name), topic_(topic), controller_(controller), degradation_mgr_(degradation_mgr) {}

    bool initialize() override {
        if (controller_ == nullptr) {
            return false;
        }
        if (controller_->getChannelCount() > limits::MAX_SERVO_CHANNELS) {
            return false;
        }
        sub_ = createSubscription<messages::ServoCommand>(topic_, &ServoBridgeNode::onCommand, this);
        return sub_ != nullptr;
    }

    void process(uint64_t) override {}

    void emergencyStop() override {
        if (controller_ != nullptr) {
            controller_->disableAll();
        }
    }

    [[nodiscard]] hal::IServoController* getController() const { return controller_; }

private:
    static constexpr float kMinimumCommandPulseUs = 400.0f;
    static constexpr float kMaximumCommandPulseUs = 2500.0f;
    static constexpr float kMaximum14BitValue = 16383.0f;

    [[nodiscard]] bool isSafetyBlocked(const messages::ServoCommand& cmd) const {
        if (degradation_mgr_ == nullptr || degradation_mgr_->getCurrentMode() < safety::DegradationMode::SAFE_STOP) {
            return false;
        }

        return cmd.command_type != messages::ServoCommand::CommandType::DISABLE;
    }

    [[nodiscard]] static bool pulseFromCommand(float value, uint16_t& pulse_us) {
        if (!std::isfinite(value) || value <= 0.0f) {
            return false;
        }
        if (value < kMinimumCommandPulseUs) {
            value = kMinimumCommandPulseUs;
        } else if (value > kMaximumCommandPulseUs) {
            value = kMaximumCommandPulseUs;
        }
        pulse_us = static_cast<uint16_t>(value);
        return true;
    }

    [[nodiscard]] static bool nonNegative14BitFromCommand(float value, uint16_t& out) {
        if (!std::isfinite(value) || value < 0.0f) {
            return false;
        }
        if (value > kMaximum14BitValue) {
            value = kMaximum14BitValue;
        }
        out = static_cast<uint16_t>(value);
        return true;
    }

    void onCommand(const messages::ServoCommand& cmd) {
        if (controller_ == nullptr) {
            return;
        }
        if (cmd.servo_id >= controller_->getChannelCount()) {
            return;
        }

        const bool degradation_blocks_motion = isSafetyBlocked(cmd);
        if (degradation_blocks_motion) {
            return;
        }

        switch (cmd.command_type) {
            case messages::ServoCommand::CommandType::SET_POSITION: {
                uint16_t pulse_us = 0;
                if (!pulseFromCommand(cmd.value, pulse_us)) {
                    return;
                }
                controller_->enable(cmd.servo_id);
                controller_->setPosition(cmd.servo_id, pulse_us);
                break;
            }
            case messages::ServoCommand::CommandType::SET_SPEED: {
                uint16_t speed = 0;
                if (!nonNegative14BitFromCommand(cmd.value, speed)) {
                    return;
                }
                controller_->setSpeed(cmd.servo_id, speed);
                break;
            }
            case messages::ServoCommand::CommandType::ENABLE:
                controller_->enable(cmd.servo_id);
                break;
            case messages::ServoCommand::CommandType::DISABLE:
                controller_->disable(cmd.servo_id);
                break;
        }
    }

    const char* topic_;
    hal::IServoController* controller_;
    safety::DegradationManager* degradation_mgr_;
    core::TypedSubscriptionPtr<messages::ServoCommand> sub_;
};

}  // namespace chopper::nodes
