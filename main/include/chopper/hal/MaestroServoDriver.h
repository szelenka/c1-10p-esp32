#pragma once

#include "chopper/hal/IServoController.h"
#include "esp_log.h"

// Forward-declare to avoid pulling in the full Pololu Maestro header.
class MiniMaestro;

namespace chopper {
namespace hal {

/**
 * HAL servo controller that wraps a Pololu Maestro (Mini Maestro)
 * servo controller board.
 *
 * Uses fixed-size arrays instead of std::vector for channel state.
 * One instance per physical Maestro board.
 */
class MaestroServoDriver : public IServoController {
public:
    static constexpr uint8_t kMaxChannels = 24;

    /**
     * @param maestro      Reference to the MiniMaestro library object.
     * @param channelCount Number of active channels on this board.
     * @param name         Driver name for diagnostics.
     */
    MaestroServoDriver(MiniMaestro& maestro, uint8_t channelCount, const char* name)
        : m_maestro(&maestro)
        , m_channelCount(channelCount > kMaxChannels ? kMaxChannels : channelCount)
        , m_name(name)
    {
        for (uint8_t i = 0; i < kMaxChannels; i++) {
            m_positions[i] = 0;
            m_targets[i] = 0;
            m_previousTargets[i] = 0;
            m_enabled[i] = false;
        }
    }

    // -- IDriver interface --

    DriverStatus init() override {
        ESP_LOGI(m_name, "init %d channels", m_channelCount);
        m_status = DriverStatus::kReady;
        return m_status;
    }

    void update() override {
        if (m_status != DriverStatus::kReady && m_status != DriverStatus::kDegraded) {
            return;
        }

        // Build target array: convert positions to quarter-microsecond units
        // as required by the Maestro setMultiTarget command.
        bool changed = false;
        for (uint8_t i = 0; i < m_channelCount; i++) {
            uint16_t target = m_enabled[i] ? static_cast<uint16_t>(m_positions[i] * 4) : 0;
            m_targets[i] = target;
            if (target != m_previousTargets[i]) {
                changed = true;
            }
        }

        if (changed) {
            maestroSetMultiTarget(m_channelCount, 0, m_targets);
            for (uint8_t i = 0; i < m_channelCount; i++) {
                m_previousTargets[i] = m_targets[i];
            }
        }
    }

    DriverStatus getStatus() const override { return m_status; }
    ErrorInfo getErrorState() const override { return m_lastError; }
    const char* getName() const override { return m_name; }

    DriverStatus reset() override {
        disableAll();
        m_lastError.clear();
        m_status = DriverStatus::kReady;
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
    }

    void setAngle(uint8_t channel, float angle) override {
        if (channel >= m_channelCount) return;
        // Default mapping: 0 degrees = 500us, 180 degrees = 2500us
        // Subclass or configure for different ranges.
        uint16_t pulse = static_cast<uint16_t>(500.0f + (angle / 180.0f) * 2000.0f);
        m_positions[channel] = pulse;
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
        // Send immediate disable to hardware (target=0 stops PWM pulses)
        maestroSetTarget(channel, 0);
    }

    void disableAll() override {
        for (uint8_t i = 0; i < m_channelCount; i++) {
            m_enabled[i] = false;
            m_positions[i] = 0;
        }
    }

    void setSpeed(uint8_t channel, uint16_t speed) override {
        if (channel >= m_channelCount) return;
        maestroSetSpeed(channel, speed);
    }

    void setAcceleration(uint8_t channel, uint16_t accel) override {
        if (channel >= m_channelCount) return;
        maestroSetAcceleration(channel, accel);
    }

    uint8_t getChannelCount() const override { return m_channelCount; }

    bool handleDiagnostic(const char* command, char* response, size_t maxLen) override {
        if (!command || !response || maxLen == 0) return false;

        if (strcmp(command, "status") == 0) {
            snprintf(response, maxLen, "%s, %d channels",
                     driverStatusToString(m_status), m_channelCount);
            return true;
        }
        if (strcmp(command, "home") == 0) {
            disableAll();
            snprintf(response, maxLen, "all channels disabled");
            return true;
        }
        return false;
    }

private:
    /// Hardware abstraction points — call through to MiniMaestro library.
    void maestroSetMultiTarget(uint8_t count, uint8_t firstChannel, const uint16_t* targets);
    void maestroSetTarget(uint8_t channel, uint16_t target);
    void maestroSetSpeed(uint8_t channel, uint16_t speed);
    void maestroSetAcceleration(uint8_t channel, uint16_t accel);

    MiniMaestro* m_maestro;
    uint8_t m_channelCount;
    const char* m_name;
    DriverStatus m_status = DriverStatus::kUninitialized;
    ErrorInfo m_lastError;

    uint16_t m_positions[kMaxChannels];
    uint16_t m_targets[kMaxChannels];
    uint16_t m_previousTargets[kMaxChannels];
    bool m_enabled[kMaxChannels];
};

} // namespace hal
} // namespace chopper
