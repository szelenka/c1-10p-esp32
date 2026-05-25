#pragma once

#include "chopper/chopper_limits.h"
#include "chopper/core/PublishingNode.h"
#include "chopper/math/Easing.h"
#include "chopper/messages/CommonMessages.h"
#include "esp_log.h"
#include "esp_timer.h"

#include <cmath>
#include <cstdint>

namespace chopper::nodes {

/**
 * Converts timed servo motion requests into immediate servo position commands.
 *
 * The bridge stays responsible for safety gating and driver I/O. This node owns
 * motion-state concerns: easing, current-position tracking, and interrupted
 * timed moves.
 */
class ServoMotionNode : public core::PublishingNode {
public:
    ServoMotionNode(const char* name, const char* input_topic, const char* output_topic, uint8_t channel_count)
        : PublishingNode(name)
        , input_topic_(input_topic)
        , output_topic_(output_topic)
        , channel_count_(clampChannelCount(channel_count)) {}

    bool initialize() override {
        move_pub_ = createPublisher<messages::ServoCommand>(output_topic_);
        move_sub_ = createSubscription<messages::ServoCommand>(input_topic_, &ServoMotionNode::onCommand, this);
        return move_pub_ != nullptr && move_sub_ != nullptr;
    }

    void process(uint64_t now_us) override {
        const uint64_t now_ms = now_us / 1000ULL;
        for (uint8_t channel = 0; channel < channel_count_; channel++) {
            auto& move = timed_moves_[channel];
            if (!move.active) {
                continue;
            }

            const uint16_t pulse_us = pulseForMove(move, now_ms);
            publishPosition(channel, pulse_us);

            if (now_ms >= move.finish_ms) {
                move.active = false;
            }
        }
    }

    void emergencyStop() override { clearTimedMoves(); }

    [[nodiscard]] double getUpdateFrequency() const override { return 50.0; }

    [[nodiscard]] bool isMoving(uint8_t channel) const {
        return channel < channel_count_ && timed_moves_[channel].active;
    }

    [[nodiscard]] uint16_t getLastPosition(uint8_t channel) const {
        if (channel >= channel_count_ || !has_last_position_[channel]) {
            return 0;
        }
        return last_position_[channel];
    }

private:
    static constexpr const char* TAG = "ServoMotion";

    struct TimedMove {
        bool active = false;
        uint16_t start_us = 0;
        uint16_t target_us = 0;
        uint64_t start_ms = 0;
        uint64_t finish_ms = 0;
        uint16_t duration_ms = 0;
    };

    static constexpr float kMinimumProgress = 0.0f;
    static constexpr float kMaximumProgress = 1.0f;

    [[nodiscard]] static uint8_t clampChannelCount(uint8_t channel_count) {
        if (channel_count > limits::MAX_SERVO_CHANNELS) {
            return static_cast<uint8_t>(limits::MAX_SERVO_CHANNELS);
        }
        return channel_count;
    }

    [[nodiscard]] static float clampProgress(float progress) {
        if (progress < kMinimumProgress) {
            return kMinimumProgress;
        }
        if (progress > kMaximumProgress) {
            return kMaximumProgress;
        }
        return progress;
    }

    [[nodiscard]] static uint16_t pulseForMove(const TimedMove& move, uint64_t now_ms) {
        if (move.duration_ms == 0 || now_ms >= move.finish_ms) {
            return move.target_us;
        }

        const uint64_t elapsed_ms = now_ms > move.start_ms ? now_ms - move.start_ms : 0;
        const float progress = clampProgress(static_cast<float>(elapsed_ms) / static_cast<float>(move.duration_ms));
        const float eased = math::Easing::CubicEaseInOut(progress);
        const float delta = static_cast<float>(move.target_us) - static_cast<float>(move.start_us);
        return static_cast<uint16_t>(static_cast<float>(move.start_us) + (delta * eased));
    }

    [[nodiscard]] uint16_t currentPosition(uint8_t channel, uint64_t now_ms, uint16_t fallback_us) const {
        if (channel >= channel_count_) {
            return fallback_us;
        }
        if (timed_moves_[channel].active) {
            return pulseForMove(timed_moves_[channel], now_ms);
        }
        if (has_last_position_[channel]) {
            return last_position_[channel];
        }
        return fallback_us;
    }

    [[nodiscard]] uint16_t adjustedDuration(uint16_t requested_duration_ms, uint16_t current_us, uint16_t start_us,
                                            uint16_t target_us) const {
        if (requested_duration_ms == 0 || start_us == target_us || current_us == start_us) {
            return requested_duration_ms;
        }

        const float full_distance = static_cast<float>(target_us) - static_cast<float>(start_us);
        if (std::fabs(full_distance) < 1.0f) {
            return requested_duration_ms;
        }

        const float travelled = static_cast<float>(current_us) - static_cast<float>(start_us);
        const float existing_progress = clampProgress(std::fabs(travelled / full_distance));
        const float remaining = static_cast<float>(requested_duration_ms) * (1.0f - existing_progress);
        if (remaining < 1.0f) {
            return 1;
        }
        return static_cast<uint16_t>(remaining);
    }

    void clearTimedMoves() {
        for (auto& move : timed_moves_) {
            move.active = false;
        }
    }

    void clearTimedMove(uint8_t channel) {
        if (channel < channel_count_) {
            timed_moves_[channel].active = false;
        }
    }

    void publishPosition(uint8_t channel, uint16_t pulse_us) {
        if (!move_pub_) {
            return;
        }

        messages::ServoCommand cmd;
        cmd.servo_id = channel;
        cmd.command_type = messages::ServoCommand::CommandType::SET_POSITION;
        cmd.value = static_cast<float>(pulse_us);
        move_pub_->publish(cmd);

        last_position_[channel] = pulse_us;
        has_last_position_[channel] = true;
    }

    void forwardImmediate(const messages::ServoCommand& cmd) {
        if (!move_pub_) {
            return;
        }

        messages::ServoCommand out = cmd;
        out.duration_ms = 0;
        out.has_start_value = false;
        out.start_value = 0.0f;
        move_pub_->publish(out);

        if (cmd.command_type == messages::ServoCommand::CommandType::SET_POSITION) {
            last_position_[cmd.servo_id] = static_cast<uint16_t>(cmd.value);
            has_last_position_[cmd.servo_id] = true;
        } else if (cmd.command_type == messages::ServoCommand::CommandType::DISABLE) {
            has_last_position_[cmd.servo_id] = false;
        }
    }

    void beginTimedMove(const messages::ServoCommand& cmd) {
        const uint64_t now_ms = static_cast<uint64_t>(esp_timer_get_time() / 1000);
        const uint16_t command_start_us =
            cmd.has_start_value ? static_cast<uint16_t>(cmd.start_value) : static_cast<uint16_t>(cmd.value);
        const uint16_t current_us = currentPosition(cmd.servo_id, now_ms, command_start_us);
        const uint16_t target_us = static_cast<uint16_t>(cmd.value);
        const uint16_t duration_ms = adjustedDuration(cmd.duration_ms, current_us, command_start_us, target_us);

        if (duration_ms == 0 || current_us == target_us) {
            clearTimedMove(cmd.servo_id);
            publishPosition(cmd.servo_id, target_us);
            return;
        }

        auto& move = timed_moves_[cmd.servo_id];
        move.active = true;
        move.start_us = current_us;
        move.target_us = target_us;
        move.start_ms = now_ms;
        move.finish_ms = now_ms + duration_ms;
        move.duration_ms = duration_ms;

        publishPosition(cmd.servo_id, current_us);
    }

    void onCommand(const messages::ServoCommand& cmd) {
        if (cmd.servo_id >= channel_count_) {
            return;
        }

        if (cmd.command_type == messages::ServoCommand::CommandType::SET_POSITION && cmd.duration_ms > 0) {
            ESP_LOGI(TAG, "%s timed move request: %s -> %s id=%u start=%ld target=%ld duration=%u has_start=%d",
                     getName(), input_topic_, output_topic_, static_cast<unsigned>(cmd.servo_id),
                     static_cast<long>(cmd.start_value), static_cast<long>(cmd.value),
                     static_cast<unsigned>(cmd.duration_ms), cmd.has_start_value ? 1 : 0);
        }

        if (cmd.command_type == messages::ServoCommand::CommandType::SET_POSITION && cmd.duration_ms > 0) {
            beginTimedMove(cmd);
            return;
        }
        clearTimedMove(cmd.servo_id);
        forwardImmediate(cmd);
    }

    const char* input_topic_;
    const char* output_topic_;
    uint8_t channel_count_;
    core::TypedPublisherPtr<messages::ServoCommand> move_pub_;
    core::TypedSubscriptionPtr<messages::ServoCommand> move_sub_;
    TimedMove timed_moves_[limits::MAX_SERVO_CHANNELS]{};
    uint16_t last_position_[limits::MAX_SERVO_CHANNELS]{};
    bool has_last_position_[limits::MAX_SERVO_CHANNELS]{};
};

}  // namespace chopper::nodes
