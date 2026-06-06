#pragma once

#include "chopper/core/PublishingNode.h"
#include "chopper/core/ParameterServer.h"
#include "chopper/dome/DomePosition.h"
#include "chopper/math/MathUtil.h"
#include "chopper/math/SlewRateLimiter.h"
#include "chopper/messages/CommonMessages.h"
#include "esp_timer.h"
#ifdef ESP_PLATFORM
#include "esp_random.h"
#endif

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace chopper::nodes {

/**
 * Node that manages dome spin and publishes dome position data.
 *
 * Manual mode: joystick X-axis from the dome controller drives the motor.
 * Tracking mode: proportional controller centers a detected face horizontally.
 *   Hold SL+SR for 2 seconds on dome controller to toggle.
 * Auto mode (kRandom): after an idle timeout, picks random targets within
 * a configurable arc and moves toward them, with probabilistic direction
 * changes and return-to-home behavior (Roam-a-Dome style).
 *
 * Priority: manual joystick > face tracking > random roam.
 * Press Home button to toggle random mode on/off.
 */
class DomeNode : public core::PublishingNode {
public:
    /**
     * @param dome_position  Dome position tracker (externally owned).
     * @param max_speed      Maximum dome spin speed (0..1).
     * @param slew_rate      Slew rate for smooth dome spin ramping.
     * @param motor_id       Motor ID for the dome spin motor.
     * @param frame_width    Camera frame width used for tracking error normalization.
     */
    explicit DomeNode(dome::DomePosition* dome_position = nullptr, float max_speed = 0.5f, float slew_rate = 1.0f,
                      uint8_t motor_id = 0, int32_t frame_width = 320)
        : PublishingNode("dome")
        , dome_position_(dome_position)
        , max_speed_(max_speed)
        , motor_id_(motor_id)
        , frame_width_(static_cast<uint16_t>(std::max(frame_width, static_cast<int32_t>(1))))
        , slew_(slew_rate) {
        // process() does dead-reckoning, auto-dome, and motor publish —
        // synchronous broker dispatch can spike on ESP32.
        setMaxExecutionTime(5000);
    }

    DomeNode(dome::DomePosition* dome_position, float max_speed, float slew_rate, uint8_t motor_id,
             bool inverted) = delete;
    DomeNode(dome::DomePosition* dome_position, float max_speed, float slew_rate, uint8_t motor_id, bool inverted,
             uint16_t frame_width) = delete;

    bool initialize() override {
        motor_pub_ = createPublisher<messages::MotorCommand>("dome/motor/cmd");
        sensor_pub_ = createPublisher<messages::SensorData>("dome/position");
        tracking_cmd_pub_ = createPublisher<messages::TrackingCommand>("openmv/tracking/cmd");
        led_pub_ = createPublisher<messages::LEDCommand>("led/dome_eye/cmd");
        dome_input_sub_ =
            createSubscription<messages::ControllerInput>("controller/dome", &DomeNode::onDomeControllerInput, this);
        drive_input_sub_ =
            createSubscription<messages::ControllerInput>("controller/drive", &DomeNode::onDriveControllerInput, this);
        vision_sub_ = createSubscription<messages::VisionResult>("vision/result", &DomeNode::onVisionResult, this);

        auto& ps = core::ParameterServer::getInstance();

        // Params are declared in DefaultParameters.h (single source of truth).
        // Just read current values and register for change notifications.
        refreshCachedParams();
        slew_rate_current_ = spin_slew_rate_;
        resetManualSpinToZero(static_cast<uint64_t>(esp_timer_get_time() / 1000ULL));
        bool listeners_ok = true;
        listeners_ok &= ps.onChange("dome.max_speed", &DomeNode::onParameterChanged, this);
        listeners_ok &= ps.onChange("dome.deadband", &DomeNode::onParameterChanged, this);
        listeners_ok &= ps.onChange("dome.spin_slew_rate", &DomeNode::onParameterChanged, this);
        listeners_ok &= ps.onChange("tracking.kp", &DomeNode::onParameterChanged, this);
        listeners_ok &= ps.onChange("tracking.max_speed", &DomeNode::onParameterChanged, this);
        listeners_ok &= ps.onChange("tracking.deadband", &DomeNode::onParameterChanged, this);
        listeners_ok &= ps.onChange("tracking.min_confidence", &DomeNode::onParameterChanged, this);
        listeners_ok &= ps.onChange("tracking.frame_width", &DomeNode::onParameterChanged, this);

        return motor_pub_ != nullptr && sensor_pub_ != nullptr && tracking_cmd_pub_ != nullptr && led_pub_ != nullptr &&
               dome_input_sub_ != nullptr && drive_input_sub_ != nullptr && vision_sub_ != nullptr && listeners_ok;
    }

    void process(uint64_t now) override {
        uint64_t now_ms = now / 1000ULL;

        // Dead-reckoning fallback: estimate dome angle from motor speed
        // when no encoder is feeding position updates.
        // If the position changed since our last tick, an encoder is active
        // — skip dead-reckoning and let the real sensor drive position.
        if (dome_position_ != nullptr && dome_position_->ready() && last_process_ms_ > 0) {
            unsigned cur_pos = dome_position_->getDomePosition();
            bool encoder_updated = (cur_pos != last_known_pos_);
            if (encoder_updated) {
                last_known_pos_ = cur_pos;
            } else {
                uint64_t dt_ms = now_ms - last_process_ms_;
                if (dt_ms > 0 && std::fabs(last_published_speed_) > 0.001f) {
                    constexpr float kDegreesPerSecAtFull = 72.0f;
                    float degrees = last_published_speed_ * kDegreesPerSecAtFull * static_cast<float>(dt_ms) / 1000.0f;
                    long cur = static_cast<long>(cur_pos);
                    unsigned next = dome::DomePosition::normalize(cur + static_cast<long>(degrees));
                    dome_position_->update(next, now_ms);
                    last_known_pos_ = next;
                }
            }
        }
        last_process_ms_ = now_ms;

        // Publish dome position if available
        if ((dome_position_ != nullptr) && dome_position_->ready()) {
            messages::SensorData sd;
            sd.sensor_id = 0;
            sd.sensor_type = messages::SensorData::SensorType::POSITION;
            sd.value = static_cast<float>(dome_position_->getDomePosition());
            sd.is_valid = true;
            sensor_pub_->publish(sd);
        }

        // Update auto-dome request (sets auto_speed_)
        auto_speed_ = 0.0f;
        expireStaleTracking(now_ms);
        processAutoDome(now_ms);

        // Resolve priority: manual > tracking > auto — single publish per tick
        resolveAndPublish();
    }

    void emergencyStop() override {
        if (motor_pub_) {
            messages::MotorCommand cmd;
            cmd.motor_id = motor_id_;
            cmd.command_type = messages::MotorCommand::CommandType::EMERGENCY_STOP;
            cmd.value = 0.0f;
            motor_pub_->publish(cmd);
        }
    }

    [[nodiscard]] double getUpdateFrequency() const override { return 20.0; }

    /**
     * Set dome spin from two buttons (drive L = rotate left, dome R = rotate right).
     * @param rotate_left  Drive controller L button pressed.
     * @param rotate_right Dome controller R button pressed.
     * @param now_ms       Current time in ms for slew rate limiter.
     */
    void setDomeSpin(bool rotate_left, bool rotate_right, uint64_t now_ms) {
        last_time_ms_ = now_ms;
        drive_rotate_left_ = rotate_left;
        dome_rotate_right_ = rotate_right;
        dome_analog_rotate_ = 0.0f;
        updateDomeSpin();
        resolveAndPublish();
    }

    [[nodiscard]] dome::DomePosition* getDomePosition() const { return dome_position_; }
    [[nodiscard]] bool isRandomModeEnabled() const { return random_mode_enabled_; }
    [[nodiscard]] bool isTrackingEnabled() const { return tracking_enabled_; }
    [[nodiscard]] bool isIdle() const { return idle_; }
    [[nodiscard]] bool hasDomeMovedManually() const { return dome_has_moved_manually_; }
    [[nodiscard]] float getTrackingError() const { return tracking_last_error_; }
    [[nodiscard]] float getTrackingSpeed() const { return tracking_last_speed_; }

    /// Allow tests to inject time
    void setTime(uint64_t ms) { last_time_ms_ = ms; }

    /// Allow tests to force manual-move gate open
    void setDomeMovedManually(bool moved) { dome_has_moved_manually_ = moved; }

protected:
    bool onActivate() override {
        publishEyeColor(eyeColorForIndex(eye_color_index_));
        return true;
    }

private:
    // ── Manual input handling ───────────────────────────────────────────

    void onDriveControllerInput(const messages::ControllerInput& input) {
        const auto now_ms = static_cast<uint64_t>(esp_timer_get_time() / 1000ULL);
        last_time_ms_ = now_ms;
        if (isControllerUnavailable(input)) {
            drive_rotate_left_ = false;
            if (!dome_rotate_right_ && std::fabs(dome_analog_rotate_) <= 0.001f) {
                resetManualSpinToZero(now_ms);
            } else {
                updateDomeSpin();
            }
            return;
        }

        drive_rotate_left_ = input.has_intents ? input.intent_dome_rotate_left : input.button_l2;
        updateDomeSpin();
    }

    void onDomeControllerInput(const messages::ControllerInput& input) {
        if (!motor_pub_) {
            return;
        }

        auto now_ms = static_cast<uint64_t>(esp_timer_get_time() / 1000ULL);
        last_time_ms_ = now_ms;

        if (isControllerUnavailable(input)) {
            dome_rotate_right_ = false;
            dome_analog_rotate_ = 0.0f;
            last_random_toggle_ = false;
            last_eye_toggle_ = false;
            last_tracking_toggle_ = false;
            disableAutonomousDomeMotion();
            if (!drive_rotate_left_) {
                resetManualSpinToZero(now_ms);
            } else {
                updateDomeSpin();
            }
            return;
        }

        // Face tracking toggle: one-shot from BluepadInputNode after 2s hold
        handleTrackingToggle(input);

        // Random mode toggle: double-click right thumb stick
        handleRandomToggle(input, now_ms);

        // Eye color toggle: ZR button
        handleEyeColorToggle(input);

        // Dome rotate right from dome controller button
        dome_rotate_right_ = input.has_intents ? input.intent_dome_rotate_right : input.button_l2;
        dome_analog_rotate_ = math::ApplyDeadband(input.axis_rx_normalized, deadband_);
        updateDomeSpin();
    }

    void updateDomeSpin() {
        if (!motor_pub_) {
            return;
        }

        auto now_ms = last_time_ms_;

        float target = 0.0f;
        if (std::fabs(dome_analog_rotate_) > 0.001f) {
            target = -dome_analog_rotate_ * max_speed_;
        } else if (drive_rotate_left_ && !dome_rotate_right_) {
            target = max_speed_;
        } else if (!drive_rotate_left_ && dome_rotate_right_) {
            target = -max_speed_;
        }

        if (std::fabs(spin_slew_rate_ - slew_rate_current_) > 0.001f) {
            slew_rate_current_ = spin_slew_rate_;
            slew_.Reset(slew_rate_current_, -slew_rate_current_, slew_.LastValue());
        }
        if (std::fabs(target) <= 0.001f) {
            resetManualSpinToZero(now_ms);
            return;
        }
        manual_speed_ = slew_.Calculate(target, now_ms);

        last_manual_input_ms_ = now_ms;
        dome_has_moved_manually_ = true;
        if (idle_) {
            idle_ = false;
            auto_target_valid_ = false;
            if (dome_position_ != nullptr) {
                dome_position_->resetDefaultMode(now_ms);
            }
        }
    }

    void handleRandomToggle(const messages::ControllerInput& input, uint64_t now_ms) {
        const bool pressed = input.has_intents ? input.intent_dome_random_toggle : input.misc_select;
        if (pressed && !last_random_toggle_) {
            random_mode_enabled_ = !random_mode_enabled_;
            if (dome_position_ != nullptr) {
                if (random_mode_enabled_) {
                    dome_position_->setDomeDefaultMode(dome::DomePosition::kRandom, now_ms);
                } else {
                    dome_position_->setDomeDefaultMode(dome::DomePosition::kOff, now_ms);
                    auto_target_valid_ = false;
                }
            }
        }
        last_random_toggle_ = pressed;
    }

    // ── Eye color toggle ──────────────────────────────────────────────────

    void handleEyeColorToggle(const messages::ControllerInput& input) {
        const bool pressed = input.has_intents ? input.intent_eye_color_toggle : input.button_r2;
        if (pressed && !last_eye_toggle_) {
            eye_color_index_ = static_cast<uint8_t>((eye_color_index_ + 1) % EYE_COLOR_COUNT);
            publishEyeColor(eyeColorForIndex(eye_color_index_));
        }
        last_eye_toggle_ = pressed;
    }

    void publishEyeColor(const messages::LEDCommand::Color& color) {
        if (!led_pub_) {
            return;
        }
        messages::LEDCommand cmd;
        cmd.command_type = messages::LEDCommand::CommandType::SET_COLOR;
        cmd.color = color;
        cmd.led_id = LED_ID_RIGHT_EYE;
        led_pub_->publish(cmd);
        cmd.led_id = LED_ID_CENTER_EYE;
        led_pub_->publish(cmd);
    }

    static messages::LEDCommand::Color eyeColorForIndex(uint8_t index) {
        messages::LEDCommand::Color color;
        switch (index) {
            case EYE_COLOR_PURPLE:
                color.red = 255;
                color.blue = 255;
                break;
            case EYE_COLOR_RED:
                color.red = 255;
                break;
            case EYE_COLOR_YELLOW:
                color.red = 255;
                color.green = 255;
                break;
            case EYE_COLOR_GREEN:
                color.green = 255;
                break;
            case EYE_COLOR_BLUE:
            default:
                color.blue = 255;
                break;
        }
        return color;
    }

    static bool isControllerUnavailable(const messages::ControllerInput& input) {
        return !input.is_connected && !input.has_data;
    }

    void resetManualSpinToZero(uint64_t now_ms) {
        last_time_ms_ = now_ms;
        manual_speed_ = 0.0f;
        dome_analog_rotate_ = 0.0f;
        slew_.Reset(slew_rate_current_, -slew_rate_current_, 0.0f);
        (void)slew_.Calculate(0.0f, now_ms);
    }

    void disableAutonomousDomeMotion() {
        const bool was_tracking = tracking_enabled_;
        tracking_enabled_ = false;
        clearTrackingMotion();
        if (was_tracking && tracking_cmd_pub_) {
            messages::TrackingCommand cmd(false);
            tracking_cmd_pub_->publish(cmd);
        }
        random_mode_enabled_ = false;
        auto_speed_ = 0.0f;
        auto_target_valid_ = false;
        auto_movement_started_ = false;
        auto_go_home_ = false;
        if (dome_position_ != nullptr) {
            dome_position_->setDomeDefaultMode(dome::DomePosition::kOff, last_time_ms_);
        }
    }

    // ── Face tracking ────────────────────────────────────────────────────

    void handleTrackingToggle(const messages::ControllerInput& input) {
        const bool toggle = input.has_intents ? input.intent_face_tracking_toggle : false;
        if (toggle && !last_tracking_toggle_) {
            tracking_enabled_ = !tracking_enabled_;
            clearTrackingMotion();
            // Notify OpenMV to start/stop sending vision results
            if (tracking_cmd_pub_) {
                messages::TrackingCommand cmd(tracking_enabled_);
                tracking_cmd_pub_->publish(cmd);
            }
        }
        last_tracking_toggle_ = toggle;
    }

    void onVisionResult(const messages::VisionResult& vision) {
        if (!tracking_enabled_ || !motor_pub_) {
            return;
        }

        tracking_last_result_ms_ = static_cast<uint64_t>(esp_timer_get_time() / 1000ULL);

        float speed = 0.0f;

        if (vision.detected && vision.confidence >= tracking_min_confidence_) {
            float half_width = static_cast<float>(frame_width_) * 0.5f;
            float error = static_cast<float>(vision.center_x) / half_width;
            error = std::clamp(error, -1.0f, 1.0f);

            if (std::fabs(error) > tracking_deadband_) {
                // Positive camera X means the target is right of center; the
                // manual right-rotation command convention is negative speed.
                speed = -tracking_kp_ * error;
                speed = std::clamp(speed, -tracking_max_speed_, tracking_max_speed_);
            }
            tracking_last_error_ = error;
        } else {
            tracking_last_error_ = 0.0f;
        }

        tracking_last_speed_ = speed;
        tracking_speed_ = speed;
    }

    void expireStaleTracking(uint64_t now_ms) {
        if (!tracking_enabled_ || tracking_last_result_ms_ == 0 || now_ms < tracking_last_result_ms_) {
            return;
        }
        if ((now_ms - tracking_last_result_ms_) > kTrackingVisionTimeoutMs) {
            clearTrackingMotion();
        }
    }

    void clearTrackingMotion() {
        tracking_speed_ = 0.0f;
        tracking_last_error_ = 0.0f;
        tracking_last_speed_ = 0.0f;
        tracking_last_result_ms_ = 0;
    }

    // ── Auto-dome logic (runs in process()) ─────────────────────────────

    void processAutoDome(uint64_t now) {
        if (dome_position_ == nullptr || !dome_position_->ready()) {
            return;
        }
        if (!random_mode_enabled_) {
            return;
        }

        // Auto-safety: require at least one manual move before auto starts
        bool auto_safety = true;
        auto& ps = core::ParameterServer::getInstance();
        (void)ps.get("dome.auto_safety", auto_safety);
        if (auto_safety && !dome_has_moved_manually_) {
            return;
        }

        // Check idle transition
        auto mode = dome_position_->getDomeMode();
        if (mode == dome::DomePosition::kOff) {
            return;
        }

        uint32_t min_delay_ms = dome_position_->getDomeMinDelay() * 1000U;

        // Still has recent manual input — not idle yet
        if (last_manual_input_ms_ != 0 && (now - last_manual_input_ms_) < min_delay_ms) {
            return;
        }

        // Transition to idle
        if (!idle_) {
            idle_ = true;
            auto_movement_started_ = false;
        }

        if (mode == dome::DomePosition::kRandom) {
            processRandomMode(now);
        } else if (mode == dome::DomePosition::kHome) {
            processHomeMode(now);
        }
    }

    void processRandomMode(uint64_t now) {
        unsigned pos = dome_position_->getDomePosition();
        unsigned home = dome_position_->getDomeHome();
        unsigned fudge = dome_position_->getDomeFudge();
        float speed = dome_position_->getDomeAutoSpeed();
        uint32_t min_delay_ms = dome_position_->getDomeAutoMinDelay() * 1000U;
        uint32_t max_delay_ms = dome_position_->getDomeAutoMaxDelay() * 1000U;

        // First entry into random — schedule first move
        if (!auto_target_valid_ && next_auto_move_ms_ == 0) {
            next_auto_move_ms_ = now + domeRandom(min_delay_ms, max_delay_ms);
            auto_go_home_ = false;
            auto_left_ = (domeRandom(0, 2) == 0);
            return;
        }

        // Waiting for next move
        if (!auto_target_valid_) {
            if (now < next_auto_move_ms_) {
                dome_position_->resetWatchdog(now);
                return;
            }
            pickRandomTarget(now, home, min_delay_ms, max_delay_ms);
            if (!auto_target_valid_) {
                return;  // "do nothing" was chosen
            }
            dome_position_->resetWatchdog(now);
            auto_movement_started_ = true;
        }

        // Moving toward target
        if (auto_target_valid_) {
            // Watchdog: if dome position hasn't changed, treat as "arrived"
            // and schedule the next move.  Without an encoder the position
            // never updates, so this is the normal completion path.
            if (auto_movement_started_ && dome_position_->isTimeout(now)) {
                next_auto_move_ms_ = now + domeRandom(min_delay_ms, max_delay_ms);
                auto_target_valid_ = false;
                auto_movement_started_ = false;
                return;
            }

            float motor_out = 0.0f;
            if (moveDomeToTarget(pos, auto_target_pos_, fudge, speed, motor_out)) {
                // Arrived — schedule next move
                next_auto_move_ms_ = now + domeRandom(min_delay_ms, max_delay_ms);
                auto_target_valid_ = false;
                auto_movement_started_ = false;
            } else {
                auto_speed_ = motor_out;
            }
        }
    }

    void pickRandomTarget(uint64_t now, unsigned home, uint32_t min_delay_ms, uint32_t max_delay_ms) {
        unsigned auto_left = dome_position_->getDomeAutoLeft();
        unsigned auto_right = dome_position_->getDomeAutoRight();

        if (auto_go_home_) {
            auto_target_pos_ = home;
            auto_target_valid_ = true;
            auto_go_home_ = false;
            auto_left_ = (domeRandom(0, 2) == 0);
            return;
        }

        // 10% chance: do nothing, wait again
        if (domeRandom(0, 100) < 10) {
            next_auto_move_ms_ = now + domeRandom(min_delay_ms, max_delay_ms);
            return;
        }

        int32_t random_move_min = 5;
        auto& ps = core::ParameterServer::getInstance();
        (void)ps.get("dome.random_move_min", random_move_min);
        unsigned min_degrees = static_cast<unsigned>(std::max(random_move_min, static_cast<int32_t>(0)));

        if (auto_left_) {
            unsigned distance = (auto_left > 0) ? domeRandom(min_degrees, auto_left) : min_degrees;
            auto_target_pos_ = dome::DomePosition::normalize(static_cast<long>(home) - static_cast<long>(distance));
        } else {
            unsigned distance = (auto_right > 0) ? domeRandom(min_degrees, auto_right) : min_degrees;
            auto_target_pos_ = dome::DomePosition::normalize(static_cast<long>(home) + static_cast<long>(distance));
        }
        auto_target_valid_ = true;

        // 10% chance to go home next time
        auto_go_home_ = (domeRandom(0, 100) < 10);
        // 10% chance to switch direction
        if (domeRandom(0, 100) < 10) {
            auto_left_ = !auto_left_;
        }
    }

    void processHomeMode(uint64_t now) {
        unsigned pos = dome_position_->getDomePosition();
        unsigned home = dome_position_->getDomeHome();
        unsigned fudge = dome_position_->getDomeFudge();
        float speed = dome_position_->getDomeSpeedHome();

        float motor_out = 0.0f;
        if (moveDomeToTarget(pos, home, fudge, speed, motor_out)) {
            // At home — revert to default mode
            dome_position_->resetDefaultMode(now);
        } else {
            auto_speed_ = motor_out;
        }
    }

    // ── Helpers ─────────────────────────────────────────────────────────

    /**
     * Compute motor output to move dome toward a target position.
     * Decelerates as it approaches.
     * @return true if target is reached.
     */
    bool moveDomeToTarget(unsigned pos, unsigned target, unsigned fudge, float speed, float& motor_out) {
        if (dome_position_->isAtPosition(static_cast<long>(target))) {
            motor_out = 0.0f;
            return true;
        }
        int dist = dome::DomePosition::shortestDistance(static_cast<int>(pos), static_cast<int>(target));
        float abs_dist = static_cast<float>(std::abs(dist));

        float decel = 50.0f;
        auto& ps = core::ParameterServer::getInstance();
        (void)ps.get("dome.decel_scale", decel);

        if (abs_dist <= decel) {
            speed *= (abs_dist / decel);
        }
        speed = std::max(speed, dome_position_->getDomeMinSpeed());
        motor_out = (dist > 0) ? -speed : speed;
        return false;
    }

    /// Priority resolution: manual > tracking > auto.
    /// Called once per process() tick — the only path that publishes motor commands.
    void resolveAndPublish() {
        float speed = 0.0f;
        if (std::fabs(manual_speed_) > 0.001f) {
            speed = manual_speed_;
        } else if (tracking_enabled_) {
            speed = tracking_speed_;
        } else {
            speed = auto_speed_;
        }
        publishMotorSpeed(speed);
    }

    void publishMotorSpeed(float speed) {
        if (!motor_pub_) {
            return;
        }
        last_published_speed_ = speed;
        messages::MotorCommand cmd;
        cmd.motor_id = motor_id_;
        cmd.command_type = messages::MotorCommand::CommandType::SET_SPEED;
        cmd.value = speed;
        motor_pub_->publish(cmd);
    }

    /**
     * Generate a random number in [min_val, max_val).
     * Uses esp_random() on device, rand() in host tests.
     */
    static uint32_t domeRandom(uint32_t min_val, uint32_t max_val) {
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
        auto* self = static_cast<DomeNode*>(context);
        self->refreshCachedParams();
    }

    void refreshCachedParams() {
        auto& ps = core::ParameterServer::getInstance();
        (void)ps.get("dome.max_speed", max_speed_);
        (void)ps.get("dome.deadband", deadband_);
        (void)ps.get("dome.spin_slew_rate", spin_slew_rate_);
        (void)ps.get("tracking.kp", tracking_kp_);
        (void)ps.get("tracking.max_speed", tracking_max_speed_);
        (void)ps.get("tracking.deadband", tracking_deadband_);
        int32_t conf = 50;
        (void)ps.get("tracking.min_confidence", conf);
        tracking_min_confidence_ =
            static_cast<uint8_t>(std::clamp(conf, static_cast<int32_t>(0), static_cast<int32_t>(255)));
        int32_t fw = static_cast<int32_t>(frame_width_);
        (void)ps.get("tracking.frame_width", fw);
        frame_width_ = static_cast<uint16_t>(std::max(fw, static_cast<int32_t>(1)));

        // Sync home position from parameter to DomePosition
        int32_t home_pos = 0;
        if (ps.get("dome.home_position", home_pos) && dome_position_ != nullptr) {
            dome_position_->setDomeHomePosition(home_pos);
        }
    }

    dome::DomePosition* dome_position_;
    float max_speed_;
    float deadband_ = 0.05f;
    uint8_t motor_id_;
    uint16_t frame_width_;
    float spin_slew_rate_ = 2.0f;
    float slew_rate_current_ = 1.0f;
    math::SlewRateLimiter slew_;

    core::TypedPublisherPtr<messages::MotorCommand> motor_pub_;
    core::TypedPublisherPtr<messages::SensorData> sensor_pub_;
    core::TypedPublisherPtr<messages::TrackingCommand> tracking_cmd_pub_;
    core::TypedPublisherPtr<messages::LEDCommand> led_pub_;
    core::TypedSubscriptionPtr<messages::ControllerInput> dome_input_sub_;
    core::TypedSubscriptionPtr<messages::ControllerInput> drive_input_sub_;
    core::TypedSubscriptionPtr<messages::VisionResult> vision_sub_;

    // Priority-resolved motor speed requests (written by each source,
    // resolved once per tick in resolveAndPublish)
    float manual_speed_ = 0.0f;
    float tracking_speed_ = 0.0f;
    float auto_speed_ = 0.0f;

    // Dead-reckoning state
    uint64_t last_process_ms_ = 0;
    float last_published_speed_ = 0.0f;
    unsigned last_known_pos_ = 0;

    // Auto-dome state
    uint64_t last_time_ms_ = 0;
    uint64_t last_manual_input_ms_ = 0;
    bool idle_ = true;
    bool dome_has_moved_manually_ = false;
    bool random_mode_enabled_ = false;

    // Random movement state
    unsigned auto_target_pos_ = 0;
    bool auto_target_valid_ = false;
    bool auto_go_home_ = false;
    bool auto_left_ = false;
    uint64_t next_auto_move_ms_ = 0;
    bool auto_movement_started_ = false;

    // Button-based dome rotation state (cross-controller)
    bool drive_rotate_left_ = false;
    bool dome_rotate_right_ = false;
    float dome_analog_rotate_ = 0.0f;

    // Random toggle (Home button)
    bool last_random_toggle_ = false;

    // Dome eye LED IDs (from joint_mapping.json led_links)
    static constexpr uint8_t LED_ID_RIGHT_EYE = 1;
    static constexpr uint8_t LED_ID_CENTER_EYE = 2;

    // Eye color toggle state
    static constexpr uint8_t EYE_COLOR_BLUE = 0;
    static constexpr uint8_t EYE_COLOR_PURPLE = 1;
    static constexpr uint8_t EYE_COLOR_RED = 2;
    static constexpr uint8_t EYE_COLOR_YELLOW = 3;
    static constexpr uint8_t EYE_COLOR_GREEN = 4;
    static constexpr uint8_t EYE_COLOR_COUNT = 5;
    uint8_t eye_color_index_ = EYE_COLOR_BLUE;
    bool last_eye_toggle_ = false;

    // Face tracking state
    bool tracking_enabled_ = false;
    bool last_tracking_toggle_ = false;
    float tracking_kp_ = 0.5f;
    float tracking_max_speed_ = 0.4f;
    float tracking_deadband_ = 0.05f;
    uint8_t tracking_min_confidence_ = 50;
    float tracking_last_error_ = 0.0f;
    float tracking_last_speed_ = 0.0f;
    uint64_t tracking_last_result_ms_ = 0;
    static constexpr uint64_t kTrackingVisionTimeoutMs = 500;
};

}  // namespace chopper::nodes
