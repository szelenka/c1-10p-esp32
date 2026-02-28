#pragma once

#include "chopper/hal/IMotorDriver.h"
#include <cstring>

namespace chopper {
namespace hal {

/**
 * Mock motor driver for testing.
 *
 * Records all speed commands in a fixed-size history buffer for test assertions.
 * No hardware access — always reports kReady after init().
 */
class MockMotorDriver : public IMotorDriver {
public:
    static constexpr uint32_t kHistorySize = 64;

    explicit MockMotorDriver(const char* name)
        : m_name(name)
    {
        memset(m_setHistory, 0, sizeof(m_setHistory));
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
        m_speed = 0.0f;
        m_lastError.clear();
        m_status = DriverStatus::kReady;
        m_resetCount++;
        return m_status;
    }

    void shutdown() override {
        m_speed = 0.0f;
        m_status = DriverStatus::kDisabled;
    }

    // -- IMotorDriver interface --

    void set(float speed) override {
        if (speed > 1.0f) speed = 1.0f;
        if (speed < -1.0f) speed = -1.0f;
        m_speed = speed;
        m_setHistory[m_historyIdx % kHistorySize] = speed;
        m_historyIdx++;
    }

    float get() const override { return m_speed; }

    void setInverted(bool inverted) override { m_inverted = inverted; }
    bool isInverted() const override { return m_inverted; }

    void disable() override {
        m_speed = 0.0f;
        m_disabled = true;
    }

    void stop() override {
        m_speed = 0.0f;
    }

    // -- Test instrumentation --

    /// Inject an error state for testing error handling paths.
    void injectError(uint16_t code, const char* msg) {
        m_lastError.set(code, 0, msg);
        m_status = DriverStatus::kError;
    }

    /// Force a specific status for testing.
    void setStatus(DriverStatus status) { m_status = status; }

    /// Get the speed that was set at a given history index.
    float getSpeedAt(uint32_t index) const {
        return m_setHistory[index % kHistorySize];
    }

    /// Get the total number of set() calls.
    uint32_t getSetCount() const { return m_historyIdx; }

    uint32_t getInitCount() const { return m_initCount; }
    uint32_t getUpdateCount() const { return m_updateCount; }
    uint32_t getResetCount() const { return m_resetCount; }
    bool wasDisabled() const { return m_disabled; }

private:
    const char* m_name;
    float m_speed = 0.0f;
    bool m_inverted = false;
    bool m_disabled = false;
    DriverStatus m_status = DriverStatus::kUninitialized;
    ErrorInfo m_lastError;

    float m_setHistory[kHistorySize];
    uint32_t m_historyIdx = 0;
    uint32_t m_initCount = 0;
    uint32_t m_updateCount = 0;
    uint32_t m_resetCount = 0;
};

} // namespace hal
} // namespace chopper
