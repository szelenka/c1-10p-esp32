#pragma once

#include "chopper/hal/IDriver.h"

namespace chopper {
namespace hal {

/**
 * Audio driver interface for sound playback.
 */
class IAudioDriver : public IDriver {
public:
    ~IAudioDriver() override = default;

    /// Trigger playback of a specific track by number.
    virtual void trigger(uint8_t track) = 0;

    /// Trigger a random sound from the configured sound list.
    virtual void triggerRandom() = 0;

    /// Set playback volume (0-255, device-specific mapping).
    virtual void setVolume(uint8_t volume) = 0;

    /// Get current volume level.
    virtual uint8_t getVolume() const = 0;

    /// Check if audio is currently playing.
    virtual bool isPlaying() const = 0;

    /// Stop current playback.
    virtual void stop() = 0;
};

} // namespace hal
} // namespace chopper
