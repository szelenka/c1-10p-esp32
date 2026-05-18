#pragma once

#include "chopper/core/PublishingNode.h"
#include "chopper/hal/IAudioDriver.h"
#include "chopper/messages/CommonMessages.h"
#include "chopper/safety/DegradationManager.h"

namespace chopper::nodes {

/**
 * Bridge node: subscribes to AudioCommand messages and forwards
 * them to an IAudioDriver instance.
 */
class AudioBridgeNode : public core::PublishingNode {
public:
    AudioBridgeNode(const char* name, const char* topic, hal::IAudioDriver* driver,
                    safety::DegradationManager* degradation_mgr = nullptr)
        : PublishingNode(name), topic_(topic), driver_(driver), degradation_mgr_(degradation_mgr) {}

    bool initialize() override {
        if (driver_ == nullptr) {
            return false;
        }
        sub_ = createSubscription<messages::AudioCommand>(topic_, &AudioBridgeNode::onCommand, this);
        return sub_ != nullptr;
    }

    void process(uint64_t) override {}

    void emergencyStop() override {
        if (driver_ != nullptr) {
            driver_->stop();
        }
    }

    [[nodiscard]] hal::IAudioDriver* getDriver() const { return driver_; }

private:
    void onCommand(const messages::AudioCommand& cmd) {
        if (driver_ == nullptr) {
            return;
        }

        if (degradation_mgr_ != nullptr && degradation_mgr_->getCurrentMode() >= safety::DegradationMode::SAFE_STOP) {
            return;
        }

        switch (cmd.command_type) {
            case messages::AudioCommand::CommandType::PLAY_TRACK:
                driver_->trigger(static_cast<uint8_t>(cmd.track_id));
                break;
            case messages::AudioCommand::CommandType::STOP:
                driver_->stop();
                break;
            case messages::AudioCommand::CommandType::SET_VOLUME:
                driver_->setVolume(cmd.volume);
                break;
            default:
                break;
        }
    }

    const char* topic_;
    hal::IAudioDriver* driver_;
    safety::DegradationManager* degradation_mgr_;
    core::TypedSubscriptionPtr<messages::AudioCommand> sub_;
};

}  // namespace chopper::nodes
