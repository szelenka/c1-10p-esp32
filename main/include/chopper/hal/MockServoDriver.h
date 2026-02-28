#pragma once

#include "chopper/hal/IServoController.h"
#include <cstring>

namespace chopper {
namespace hal {

/**
 * Mock servo controller for testing.
 *
 * Stores positions in a fixed-size array and logs all position changes.
 * No hardware access — always reports kReady after init().
 */
class MockServoDriver : public IServoController {
public:
    static constexpr uint8_t kMaxChannels = 24;
    static constexpr uint32_t kLogSize = 128;

    /// Entry in the position change log.
    struct LogEntry {
        uint8_t channel;
        uint16_t pulse_us;
    };

    explicit MockServoDriver(const char* name, uint8_t channelCount = 12)
        : m_name(name)
        , m_channelCount(channelCount > kMaxChannels ? kMaxChannels : channelCount)
    {
        memset(m_positions, 0, sizeof(m_positions));
        memset(m_enabled, 0, sizeof(m_enabled));
        memset(m_log, 0, sizeof(m_log));
    }

    // -- IDriver interface --

    DriverStatus init() override {
        m_status = DriverStatus::kReady;
        m_initCount++;
        return m_status;
    }

    void update() override {
        m_updateCount++;
    }

    DriverStatus getStatus() const override { return m_status; }
    ErrorInfo getErrorState() const override { return m_lastError; }
    const char* getName() const override { return m_name; }

    DriverStatus reset() override {
        disableAll();
        m_lastError.clear();
        m_status = DriverStatus::kReady;
        m_resetCount++;
        return m_status;
    }

    void shutdown() override {
        disableAll();
        m_status = DriverStatus::kDisabled;
    }

    // -- IServoController interface --

    void setPosition(uint8_t channel, uint16_t pulse_us) override {
        if (channel >= m_channelCount) return;
        m_positions[channel] = pulse_us;
        logChange(channel, pulse_us);
    }

    void setAngle(uint8_t channel, float angle) override {
        if (channel >= m_channelCount) return;
        uint16_t pulse = static_cast<uint16_t>(500.0f + (angle / 180.0f) * 2000.0f);
        m_positions[channel] = pulse;
        logChange(channel, pulse);
    }

    uint16_t getPosition(uint8_t channel) const override {
        if (channel >= m_channelCount) return 0;
        return m_positions[channel];
    }

    void enable(uint8_t channel) override {
        if (channel >= m_channelCount) return;
        m_enabled[channel] = true;
    }

    void disable(uint8_t channel) override {
        if (channel >= m_channelCount) return;
        m_enabled[channel] = false;
        m_positions[channel] = 0;
    }

    void disableAll() override {
        for (uint8_t i = 0; i < m_channelCount; i++) {
            m_enabled[i] = false;
            m_positions[i] = 0;
        }
    }

    void setSpeed(uint8_t channel, uint16_t speed) override {
        (void)channel; (void)speed;
    }

    void setAcceleration(uint8_t channel, uint16_t accel) override {
        (void)channel; (void)accel;
    }

    uint8_t getChannelCount() const override { return m_channelCount; }

    // -- Test instrumentation --

    void injectError(uint16_t code, const char* msg) {
        m_lastError.set(code, 0, msg);
        m_status = DriverStatus::kError;
    }

    void setStatus(DriverStatus status) { m_status = status; }

    bool isEnabled(uint8_t channel) const {
        if (channel >= m_channelCount) return false;
        return m_enabled[channel];
    }

    uint32_t getLogCount() const { return m_logIdx; }

    LogEntry getLogAt(uint32_t index) const {
        return m_log[index % kLogSize];
    }

    uint32_t getInitCount() const { return m_initCount; }
    uint32_t getUpdateCount() const { return m_updateCount; }
    uint32_t getResetCount() const { return m_resetCount; }

private:
    void logChange(uint8_t channel, uint16_t pulse_us) {
        LogEntry& entry = m_log[m_logIdx % kLogSize];
        entry.channel = channel;
        entry.pulse_us = pulse_us;
        m_logIdx++;
    }

    const char* m_name;
    uint8_t m_channelCount;
    DriverStatus m_status = DriverStatus::kUninitialized;
    ErrorInfo m_lastError;

    uint16_t m_positions[kMaxChannels];
    bool m_enabled[kMaxChannels];

    LogEntry m_log[kLogSize];
    uint32_t m_logIdx = 0;
    uint32_t m_initCount = 0;
    uint32_t m_updateCount = 0;
    uint32_t m_resetCount = 0;
};

} // namespace hal
} // namespace chopper
