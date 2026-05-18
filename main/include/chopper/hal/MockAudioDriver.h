#pragma once

#include "chopper/hal/IAudioDriver.h"
#include <cstring>

namespace chopper {
namespace hal {

/**
 * Mock audio driver for testing.
 *
 * Records all trigger calls and volume changes for test assertions.
 */
class MockAudioDriver : public IAudioDriver {
public:
    static constexpr uint32_t kLogSize = 64;

    explicit MockAudioDriver(const char* name) : m_name(name) { memset(m_triggerLog, 0, sizeof(m_triggerLog)); }

    // -- IDriver interface --

    DriverStatus init() override {
        m_status = DriverStatus::kReady;
        m_initCount++;
        return m_status;
    }

    void update() override { m_updateCount++; }

    DriverStatus getStatus() const override { return m_status; }
    ErrorInfo getErrorState() const override { return m_lastError; }
    const char* getName() const override { return m_name; }

    DriverStatus reset() override {
        m_volume = 128;
        m_playing = false;
        m_lastError.clear();
        m_status = DriverStatus::kReady;
        return m_status;
    }

    void shutdown() override {
        m_playing = false;
        m_status = DriverStatus::kDisabled;
    }

    // -- IAudioDriver interface --

    void trigger(uint8_t track) override {
        m_triggerLog[m_triggerIdx % kLogSize] = track;
        m_triggerIdx++;
        m_playing = true;
        m_lastTrack = track;
    }

    void triggerRandom() override {
        trigger(42);  // deterministic for testing
    }

    void setVolume(uint8_t volume) override { m_volume = volume; }

    uint8_t getVolume() const override { return m_volume; }
    bool isPlaying() const override { return m_playing; }

    void stop() override { m_playing = false; }

    // -- Test instrumentation --

    uint8_t getLastTrack() const { return m_lastTrack; }
    uint32_t getTriggerCount() const { return m_triggerIdx; }

    uint8_t getTriggerAt(uint32_t index) const { return m_triggerLog[index % kLogSize]; }

    uint32_t getInitCount() const { return m_initCount; }
    uint32_t getUpdateCount() const { return m_updateCount; }

    void setStatus(DriverStatus status) { m_status = status; }

private:
    const char* m_name;
    DriverStatus m_status = DriverStatus::kUninitialized;
    ErrorInfo m_lastError;

    uint8_t m_volume = 128;
    bool m_playing = false;
    uint8_t m_lastTrack = 0;

    uint8_t m_triggerLog[kLogSize];
    uint32_t m_triggerIdx = 0;
    uint32_t m_initCount = 0;
    uint32_t m_updateCount = 0;
};

}  // namespace hal
}  // namespace chopper
