#pragma once

#include "chopper/core/PublishingNode.h"
#include "chopper/messages/CommonMessages.h"

namespace chopper::nodes {

/// Function signature for setting LED brightness (0–255).
/// On ESP32: wraps LEDC PWM.  In tests: captures output.
using LedWriteFn = void (*)(uint8_t brightness, void* context);

/**
 * Autonomous triangle-wave fade on the body LED.
 *
 * Replicates the legacy sketch.cpp behavior: brightness ramps 0→255→0
 * with a configurable period (default ~1 s).  Calls a hardware write
 * function each tick and publishes LEDCommand on "led/front/cmd" for
 * telemetry visibility.
 */
class BodyLedNode : public core::PublishingNode {
public:
    /// @param write_fn  Hardware callback to set LED PWM duty.
    /// @param context   Opaque pointer forwarded to write_fn.
    /// @param period_us Full fade cycle (0→255→0) in microseconds.
    explicit BodyLedNode(LedWriteFn write_fn, void* context = nullptr, uint64_t period_us = 1'020'000)
        : PublishingNode("body_led"), write_fn_(write_fn), write_ctx_(context), period_us_(period_us) {}

    bool initialize() override {
        if (write_fn_ == nullptr) {
            return false;
        }
        led_pub_ = createPublisher<messages::LEDCommand>("led/front/cmd");
        return led_pub_ != nullptr;
    }

    void process(uint64_t now_us) override {
        if (write_fn_ == nullptr) {
            return;
        }

        if (!started_) {
            start_us_ = now_us;
            started_ = true;
        }

        uint64_t elapsed = now_us - start_us_;
        uint64_t phase = elapsed % period_us_;

        // Triangle wave: first half ramps up, second half ramps down.
        uint64_t half = period_us_ / 2;
        uint8_t brightness = 0;
        if (phase < half) {
            brightness = static_cast<uint8_t>((phase * 255) / half);
        } else {
            brightness = static_cast<uint8_t>(((period_us_ - phase) * 255) / half);
        }

        write_fn_(brightness, write_ctx_);

        if (led_pub_) {
            messages::LEDCommand cmd;
            cmd.command_type = messages::LEDCommand::CommandType::SET_BRIGHTNESS;
            cmd.led_id = 0;
            cmd.brightness = brightness;
            cmd.color = {255, 0, 0, 0};  // red LED
            led_pub_->publish(cmd);
        }
    }

    void emergencyStop() override {
        if (write_fn_ != nullptr) {
            write_fn_(0, write_ctx_);
        }
        if (led_pub_) {
            messages::LEDCommand cmd;
            cmd.command_type = messages::LEDCommand::CommandType::TURN_OFF;
            cmd.led_id = 0;
            cmd.brightness = 0;
            led_pub_->publish(cmd);
        }
    }

private:
    LedWriteFn write_fn_ = nullptr;
    void* write_ctx_ = nullptr;
    uint64_t period_us_ = 1'020'000;
    uint64_t start_us_ = 0;
    bool started_ = false;
    core::TypedPublisherPtr<messages::LEDCommand> led_pub_;
};

}  // namespace chopper::nodes
