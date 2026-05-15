#pragma once

#include "chopper/core/PublishingNode.h"
#include "chopper/config/HardwareConfig.h"
#include "chopper/messages/CommonMessages.h"
#include "esp_timer.h"

namespace chopper::nodes {

/**
 * Maps dome-controller buttons to sound triggers.
 *
 * - A → play IMPERIALCAROLBELLS (track 255)
 * - B → play MANDOLORIAN (track 254)
 * - miscStart → play random sound from built-in pool
 * - drive SL/SR → volume up/down
 *
 * Publishes AudioCommand on "audio/cmd".
 */
class SoundNode : public core::PublishingNode {
public:
    SoundNode() : PublishingNode("sound") {}

    bool initialize() override {
        audio_pub_ = createPublisher<messages::AudioCommand>("audio/cmd");
        dome_input_sub_ =
            createSubscription<messages::ControllerInput>("controller/dome", &SoundNode::onDomeControllerInput, this);
        drive_input_sub_ =
            createSubscription<messages::ControllerInput>("controller/drive", &SoundNode::onDriveControllerInput, this);
        return audio_pub_ != nullptr && dome_input_sub_ != nullptr && drive_input_sub_ != nullptr;
    }

    void process(uint64_t) override {}

    void emergencyStop() override {
        if (audio_pub_) {
            messages::AudioCommand cmd;
            cmd.command_type = messages::AudioCommand::CommandType::STOP;
            audio_pub_->publish(cmd);
        }
    }

private:
    void onDomeControllerInput(const messages::ControllerInput& input) {
        if (!audio_pub_) {
            return;
        }

        const bool a_pressed = input.has_intents ? input.intent_sound_a : input.button_a;
        const bool b_pressed = input.has_intents ? input.intent_sound_b : input.button_b;
        const bool random_pressed = input.has_intents ? input.intent_sound_random : input.misc_start;

        // A → IMPERIALCAROLBELLS
        if (a_pressed && !last_a_) {
            messages::AudioCommand cmd;
            cmd.command_type = messages::AudioCommand::CommandType::PLAY_TRACK;
            cmd.track_id = static_cast<uint16_t>(config::sound_track::IMPERIALCAROLBELLS);
            audio_pub_->publish(cmd);
        }
        last_a_ = a_pressed;

        // B → MANDOLORIAN
        if (b_pressed && !last_b_) {
            messages::AudioCommand cmd;
            cmd.command_type = messages::AudioCommand::CommandType::PLAY_TRACK;
            cmd.track_id = static_cast<uint16_t>(config::sound_track::MANDOLORIAN);
            audio_pub_->publish(cmd);
        }
        last_b_ = b_pressed;

        // miscStart → random track from pool
        if (random_pressed && !last_misc_start_) {
            uint16_t track = pickRandomTrack();
            messages::AudioCommand cmd;
            cmd.command_type = messages::AudioCommand::CommandType::PLAY_TRACK;
            cmd.track_id = track;
            audio_pub_->publish(cmd);
        }
        last_misc_start_ = random_pressed;
    }

    void onDriveControllerInput(const messages::ControllerInput& input) {
        if (!audio_pub_) {
            return;
        }

        const bool volume_up_pressed = input.has_intents ? input.intent_volume_up : input.button_l1;
        const bool volume_down_pressed = input.has_intents ? input.intent_volume_down : input.button_r1;

        if (volume_down_pressed && !last_volume_down_ && !volume_up_pressed) {
            setVolume(clampVolume(static_cast<int16_t>(current_volume_) + config::sound::VOLUME_STEP));
        }
        if (volume_up_pressed && !last_volume_up_ && !volume_down_pressed) {
            setVolume(clampVolume(static_cast<int16_t>(current_volume_) - config::sound::VOLUME_STEP));
        }

        last_volume_down_ = volume_down_pressed;
        last_volume_up_ = volume_up_pressed;
    }

    uint16_t pickRandomTrack() {
        static constexpr int32_t kRandomPool[] = {
            config::sound_track::GRUMBLY01, config::sound_track::OKAYOKAY,        config::sound_track::OKAYFOLLOWME,
            config::sound_track::GRUMBLY02, config::sound_track::YESIWOULD,       config::sound_track::GRUMPY03,
            config::sound_track::NOW,       config::sound_track::WHATGROAN,       config::sound_track::WAH3,
            config::sound_track::CHATTY,    config::sound_track::EXTENDEDGRUMBLE, config::sound_track::GRUMBLY1,
            config::sound_track::UHOH,      config::sound_track::SWRSTINGER,      config::sound_track::PURR3,
            config::sound_track::TADA,
        };
        static constexpr size_t kPoolSize = sizeof(kRandomPool) / sizeof(kRandomPool[0]);

        // Simple LCG for determinism on embedded (no stdlib rand dependency)
        random_state_ = random_state_ * 1103515245u + 12345u;
        size_t index = (random_state_ >> 16) % kPoolSize;
        return static_cast<uint16_t>(kRandomPool[index]);
    }

    static uint8_t clampVolume(int16_t volume) {
        if (volume < static_cast<int16_t>(config::sound::VOLUME_LOUDEST)) {
            return config::sound::VOLUME_LOUDEST;
        }
        if (volume > static_cast<int16_t>(config::sound::VOLUME_QUIETEST)) {
            return config::sound::VOLUME_QUIETEST;
        }
        return static_cast<uint8_t>(volume);
    }

    void setVolume(uint8_t volume) {
        if (volume == current_volume_) {
            return;
        }

        current_volume_ = volume;

        messages::AudioCommand cmd;
        cmd.command_type = messages::AudioCommand::CommandType::SET_VOLUME;
        cmd.volume = current_volume_;
        audio_pub_->publish(cmd);
    }

    core::TypedPublisherPtr<messages::AudioCommand> audio_pub_;
    core::TypedSubscriptionPtr<messages::ControllerInput> dome_input_sub_;
    core::TypedSubscriptionPtr<messages::ControllerInput> drive_input_sub_;

    bool last_a_ = false;
    bool last_b_ = false;
    bool last_misc_start_ = false;
    bool last_volume_down_ = false;
    bool last_volume_up_ = false;
    uint8_t current_volume_ = config::sound::DEFAULT_VOLUME;
    uint32_t random_state_ = 1;
};

}  // namespace chopper::nodes
