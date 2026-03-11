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
 *
 * Publishes AudioCommand on "audio/cmd".
 */
class SoundNode : public core::PublishingNode {
public:
    SoundNode() : PublishingNode("sound") {}

    bool initialize() override {
        audio_pub_ = createPublisher<messages::AudioCommand>("audio/cmd");
        input_sub_ =
            createSubscription<messages::ControllerInput>("controller/dome", &SoundNode::onControllerInput, this);
        return audio_pub_ != nullptr && input_sub_ != nullptr;
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
    void onControllerInput(const messages::ControllerInput& input) {
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

    core::TypedPublisherPtr<messages::AudioCommand> audio_pub_;
    core::TypedSubscriptionPtr<messages::ControllerInput> input_sub_;

    bool last_a_ = false;
    bool last_b_ = false;
    bool last_misc_start_ = false;
    uint32_t random_state_ = 1;
};

}  // namespace chopper::nodes
