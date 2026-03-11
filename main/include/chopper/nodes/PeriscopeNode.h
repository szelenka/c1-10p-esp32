#pragma once

#include "chopper/core/PublishingNode.h"
#include "chopper/core/ParameterServer.h"
#include "chopper/config/HardwareConfig.h"
#include "chopper/messages/CommonMessages.h"

#include <cstdlib>

namespace chopper::nodes {

/**
 * Manages periscope lift and spin from the drive controller.
 *
 * - Lift from canonical intents: PERISCOPE_UP / PERISCOPE_DOWN.
 *   Legacy fallback: X toggles up/down.
 * - Spin from canonical intents: PERISCOPE_SPIN_LEFT / PERISCOPE_SPIN_RIGHT.
 *   Legacy fallback: A = left, Y = right.
 *
 * Spin only works when periscope is up (raised/extended).
 * When up with no manual input, auto-wander picks random spin targets
 * at a slow Maestro speed for organic-looking movement.
 *
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
        ps.declare("servo.peri_spin.auto_speed", static_cast<int32_t>(20), static_cast<int32_t>(1),
                   static_cast<int32_t>(200));
        ps.declare("servo.peri_spin.auto_min_delay", static_cast<int32_t>(2000), static_cast<int32_t>(500),
                   static_cast<int32_t>(30000));
        ps.declare("servo.peri_spin.auto_max_delay", static_cast<int32_t>(6000), static_cast<int32_t>(1000),
                   static_cast<int32_t>(60000));

        refreshCachedParams();
        bool listeners_ok = true;
        listeners_ok &= ps.onChange("servo.peri_lift.min", &PeriscopeNode::onParameterChanged, this);
        listeners_ok &= ps.onChange("servo.peri_lift.max", &PeriscopeNode::onParameterChanged, this);
        listeners_ok &= ps.onChange("servo.peri_spin.min", &PeriscopeNode::onParameterChanged, this);
        listeners_ok &= ps.onChange("servo.peri_spin.max", &PeriscopeNode::onParameterChanged, this);
        listeners_ok &= ps.onChange("servo.peri_spin.neutral", &PeriscopeNode::onParameterChanged, this);
        listeners_ok &= ps.onChange("servo.peri_spin.auto_speed", &PeriscopeNode::onParameterChanged, this);
        listeners_ok &= ps.onChange("servo.peri_spin.auto_min_delay", &PeriscopeNode::onParameterChanged, this);
        listeners_ok &= ps.onChange("servo.peri_spin.auto_max_delay", &PeriscopeNode::onParameterChanged, this);

        return servo_pub_ != nullptr && input_sub_ != nullptr && listeners_ok;
    }

    void process(uint64_t) override {
        auto now_ms = static_cast<uint64_t>(esp_timer_get_time() / 1000);
        processAutoWander(now_ms);
    }

    void emergencyStop() override {
        if (servo_pub_) {
            messages::ServoCommand cmd;
            cmd.command_type = messages::ServoCommand::CommandType::DISABLE;
            cmd.servo_id = config::servo_channel::DOME_PERISCOPE_LIFT;
            servo_pub_->publish(cmd);
            cmd.servo_id = config::servo_channel::DOME_PERISCOPE_SPIN;
            servo_pub_->publish(cmd);
        }
        resetAutoWander();
    }

    [[nodiscard]] bool isPeriscopeDown() const { return periscope_down_; }
    [[nodiscard]] int8_t getPeriscopeLocation() const { return periscope_location_; }
    [[nodiscard]] bool isAutoWanderActive() const { return auto_wander_active_; }
    [[nodiscard]] int32_t getAutoWanderTarget() const { return auto_wander_target_; }

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

        // Spin only works when periscope is raised (up)
        if (periscope_down_) {
            last_spin_left_ = spin_left_pressed;
            last_spin_right_ = spin_right_pressed;
            return;
        }

        bool manual_action = false;

        // Spin left
        if (spin_left_pressed && !last_spin_left_) {
            bool double_click = (now - last_a_time_ < kDoubleClickMs);
            last_a_time_ = now;

            messages::ServoCommand cmd;
            cmd.servo_id = config::servo_channel::DOME_PERISCOPE_SPIN;
            cmd.command_type = messages::ServoCommand::CommandType::SET_POSITION;

            if (double_click) {
                cmd.value = static_cast<float>(spin_max_);
                periscope_location_ = -1;
            } else {
                if (periscope_location_ == 0) {
                    cmd.value = static_cast<float>(spin_max_);
                    periscope_location_ = -1;
                } else if (periscope_location_ == 1) {
                    cmd.value = static_cast<float>(spin_neutral_);
                    periscope_location_ = 0;
                } else {
                    last_spin_left_ = spin_left_pressed;
                    return;
                }
            }
            setSpinSpeed(0);  // unlimited speed for manual control
            servo_pub_->publish(cmd);
            manual_action = true;
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
                cmd.value = static_cast<float>(spin_min_);
                periscope_location_ = 1;
            } else {
                if (periscope_location_ == 0) {
                    cmd.value = static_cast<float>(spin_min_);
                    periscope_location_ = 1;
                } else if (periscope_location_ == -1) {
                    cmd.value = static_cast<float>(spin_neutral_);
                    periscope_location_ = 0;
                } else {
                    last_spin_right_ = spin_right_pressed;
                    return;
                }
            }
            setSpinSpeed(0);  // unlimited speed for manual control
            servo_pub_->publish(cmd);
            manual_action = true;
        }
        last_spin_right_ = spin_right_pressed;

        if (manual_action) {
            resetAutoWander();
            last_manual_spin_ms_ = now;
        }
    }

    // ── Auto-wander ────────────────────────────────────────────────────

    void processAutoWander(uint64_t now_ms) {
        if (!servo_pub_ || periscope_down_) {
            if (auto_wander_active_) {
                resetAutoWander();
            }
            return;
        }

        // Wait for manual input to settle before starting auto-wander
        if (last_manual_spin_ms_ != 0) {
            uint64_t cooldown = static_cast<uint64_t>(auto_max_delay_ms_);
            if (now_ms - last_manual_spin_ms_ < cooldown) {
                return;
            }
        }

        // First entry — set slow speed and schedule first move
        if (!auto_wander_active_) {
            auto_wander_active_ = true;
            setSpinSpeed(static_cast<uint16_t>(auto_speed_));
            auto_wander_target_ = spin_neutral_;
            next_auto_move_ms_ = now_ms + randomInRange(static_cast<uint32_t>(auto_min_delay_ms_),
                                                        static_cast<uint32_t>(auto_max_delay_ms_));
            return;
        }

        // Waiting for next move
        if (now_ms < next_auto_move_ms_) {
            return;
        }

        // Pick a random target and send it
        pickAutoTarget();
        publishSpinPosition(static_cast<float>(auto_wander_target_));
        next_auto_move_ms_ = now_ms + randomInRange(static_cast<uint32_t>(auto_min_delay_ms_),
                                                    static_cast<uint32_t>(auto_max_delay_ms_));
    }

    void pickAutoTarget() {
        // 10% chance: return to neutral
        if (randomInRange(0, 100) < 10) {
            auto_wander_target_ = spin_neutral_;
            return;
        }

        // Pick a random PWM in [spin_min_, spin_max_]
        auto_wander_target_ =
            static_cast<int32_t>(randomInRange(static_cast<uint32_t>(spin_min_), static_cast<uint32_t>(spin_max_ + 1)));
    }

    void resetAutoWander() {
        auto_wander_active_ = false;
        next_auto_move_ms_ = 0;
    }

    // ── Helpers ─────────────────────────────────────────────────────────

    void moveLiftTo(float pwm_us) {
        messages::ServoCommand cmd;
        cmd.servo_id = config::servo_channel::DOME_PERISCOPE_LIFT;
        cmd.command_type = messages::ServoCommand::CommandType::SET_POSITION;
        cmd.value = pwm_us;
        servo_pub_->publish(cmd);
    }

    void publishSpinPosition(float pwm_us) {
        messages::ServoCommand cmd;
        cmd.servo_id = config::servo_channel::DOME_PERISCOPE_SPIN;
        cmd.command_type = messages::ServoCommand::CommandType::SET_POSITION;
        cmd.value = pwm_us;
        servo_pub_->publish(cmd);
    }

    void setSpinSpeed(uint16_t speed) {
        messages::ServoCommand cmd;
        cmd.servo_id = config::servo_channel::DOME_PERISCOPE_SPIN;
        cmd.command_type = messages::ServoCommand::CommandType::SET_SPEED;
        cmd.value = static_cast<float>(speed);
        servo_pub_->publish(cmd);
    }

    static uint32_t randomInRange(uint32_t min_val, uint32_t max_val) {
        if (max_val <= min_val) {
            return min_val;
        }
        uint32_t range = max_val - min_val;
#ifdef ESP_PLATFORM
        return min_val + (esp_random() % range);
#else
        return min_val + (static_cast<uint32_t>(std::rand()) % range);
#endif
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
        (void)ps.get("servo.peri_spin.auto_speed", auto_speed_);
        (void)ps.get("servo.peri_spin.auto_min_delay", auto_min_delay_ms_);
        (void)ps.get("servo.peri_spin.auto_max_delay", auto_max_delay_ms_);
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
    uint64_t last_manual_spin_ms_ = 0;

    // Auto-wander state
    bool auto_wander_active_ = false;
    int32_t auto_wander_target_ = 1500;
    uint64_t next_auto_move_ms_ = 0;

    // Cached parameters
    int32_t lift_min_ = 500;
    int32_t lift_max_ = 2500;
    int32_t spin_min_ = 500;
    int32_t spin_max_ = 2500;
    int32_t spin_neutral_ = 1500;
    int32_t auto_speed_ = 20;
    int32_t auto_min_delay_ms_ = 2000;
    int32_t auto_max_delay_ms_ = 6000;
};

}  // namespace chopper::nodes
