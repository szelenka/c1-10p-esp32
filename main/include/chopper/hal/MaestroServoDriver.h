#pragma once

#include "chopper/hal/IServoController.h"
#include "chopper/hal/ISerialPort.h"
#include "esp_log.h"
#include <cstring>

namespace chopper::hal {

/**
 * HAL servo controller for Pololu Mini Maestro boards.
 *
 * Implements the Pololu serial protocol directly via ISerialPort, removing
 * the Arduino MiniMaestro library dependency.
 *
 * Protocol reference: https://www.pololu.com/docs/0J40/5.c
 *   - Pololu protocol: 0xAA, device number, 7-bit command byte, channel, data bytes
 *   - Compact protocol omits the 0xAA and device bytes and keeps the command high bit
 *   - Targets are in quarter-microsecond units (e.g., 1500us = 6000)
 *   - Data encoded as two 7-bit bytes: low bits then high bits
 *
 * One instance per physical Maestro board.
 */
class MaestroServoDriver : public IServoController {
public:
    static constexpr uint8_t kMaxChannels = 24;
    static constexpr uint8_t kCompactProtocolDeviceNumber = 255;
    static constexpr uint8_t CMD_POLOLU_START = 0xAA;
    static constexpr uint16_t ERROR_SERIAL_WRITE = 1;

    // Pololu protocol command bytes
    static constexpr uint8_t CMD_SET_TARGET = 0x84;
    static constexpr uint8_t CMD_SET_SPEED = 0x87;
    static constexpr uint8_t CMD_SET_ACCELERATION = 0x89;
    static constexpr uint8_t CMD_SET_MULTI_TARGET = 0x9F;
    static constexpr uint8_t CMD_GET_POSITION = 0x90;

    /**
     * @param serial       Serial port for Maestro communication.
     * @param channelCount Number of active channels on this board.
     * @param name         Driver name for diagnostics.
     * @param deviceNumber Pololu device number (default 12 for Mini Maestro).
     */
    MaestroServoDriver(ISerialPort& serial, uint8_t channelCount, const char* name, uint8_t deviceNumber = 12)
        : m_serial(&serial)
        , m_channelCount(channelCount > kMaxChannels ? kMaxChannels : channelCount)
        , m_name(name)
        , m_deviceNumber(deviceNumber) {
        memset(m_positions, 0, sizeof(m_positions));
        memset(m_targets, 0, sizeof(m_targets));
        memset(m_previousTargets, 0, sizeof(m_previousTargets));
        memset(m_enabled, 0, sizeof(m_enabled));
    }

    // -- IDriver interface --

    DriverStatus init() override {
        m_status = DriverStatus::kReady;
        return m_status;
    }

    void update() override {
        if (m_status != DriverStatus::kReady && m_status != DriverStatus::kDegraded) {
            return;
        }

        // Build target array: convert positions to quarter-microsecond units
        bool changed = false;
        for (uint8_t i = 0; i < m_channelCount; i++) {
            uint16_t target = m_enabled[i] ? static_cast<uint16_t>(m_positions[i] * 4) : 0;
            m_targets[i] = target;
            if (target != m_previousTargets[i]) {
                changed = true;
            }
        }

        if (changed && maestroSetMultiTarget(m_channelCount, 0, m_targets)) {
            for (uint8_t i = 0; i < m_channelCount; i++) {
                m_previousTargets[i] = m_targets[i];
            }
        }
    }

    [[nodiscard]] DriverStatus getStatus() const override { return m_status; }
    [[nodiscard]] ErrorInfo getErrorState() const override { return m_lastError; }
    [[nodiscard]] const char* getName() const override { return m_name; }

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
        if (channel >= m_channelCount) {
            return;
        }
        m_positions[channel] = pulse_us;
    }

    void setAngle(uint8_t channel, float angle) override {
        if (channel >= m_channelCount) {
            return;
        }
        auto pulse = static_cast<uint16_t>(500.0f + (angle / 180.0f) * 2000.0f);
        m_positions[channel] = pulse;
    }

    [[nodiscard]] uint16_t getPosition(uint8_t channel) const override {
        if (channel >= m_channelCount) {
            return 0;
        }
        return m_positions[channel];
    }

    void enable(uint8_t channel) override {
        if (channel >= m_channelCount) {
            return;
        }
        m_enabled[channel] = true;
    }

    void disable(uint8_t channel) override {
        if (channel >= m_channelCount) {
            return;
        }
        m_enabled[channel] = false;
        m_positions[channel] = 0;
        m_targets[channel] = 0;
        // Send immediate disable (target=0 stops PWM pulses)
        if (maestroSetTarget(channel, 0)) {
            m_previousTargets[channel] = 0;
        }
    }

    void disableAll() override {
        for (uint8_t i = 0; i < m_channelCount; i++) {
            m_enabled[i] = false;
            m_positions[i] = 0;
            m_targets[i] = 0;
        }
        if ((m_status != DriverStatus::kUninitialized && m_status != DriverStatus::kDisabled) &&
            maestroSetMultiTarget(m_channelCount, 0, m_targets)) {
            for (uint8_t i = 0; i < m_channelCount; i++) {
                m_previousTargets[i] = 0;
            }
        }
    }

    void setSpeed(uint8_t channel, uint16_t speed) override {
        if (channel >= m_channelCount) {
            return;
        }
        if (!maestroSetSpeed(channel, speed)) {
            return;
        }
    }

    void setAcceleration(uint8_t channel, uint16_t accel) override {
        if (channel >= m_channelCount) {
            return;
        }
        if (!maestroSetAcceleration(channel, accel)) {
            return;
        }
    }

    [[nodiscard]] uint8_t getChannelCount() const override { return m_channelCount; }

    bool handleDiagnostic(const char* command, char* response, size_t maxLen) override {
        if ((command == nullptr) || (response == nullptr) || maxLen == 0) {
            return false;
        }

        if (strcmp(command, "status") == 0) {
            snprintf(response, maxLen, "%s, %d channels", driverStatusToString(m_status), m_channelCount);
            return true;
        }
        if (strcmp(command, "home") == 0) {
            disableAll();
            snprintf(response, maxLen, "all channels disabled");
            return true;
        }
        return false;
    }

    // -- Test / inspection accessors --

    [[nodiscard]] uint8_t getDeviceNumber() const { return m_deviceNumber; }
    [[nodiscard]] bool isEnabled(uint8_t channel) const {
        if (channel >= m_channelCount) {
            return false;
        }
        return m_enabled[channel];
    }

private:
    /**
     * Set Target (command 0x84).
     * Sends: [0xAA, device], command, channel, target_low_7bits, target_high_7bits
     * Pololu protocol command byte is 0x04; compact protocol command byte is 0x84.
     * Target is in quarter-microsecond units.
     */
    [[nodiscard]] bool maestroSetTarget(uint8_t channel, uint16_t target) {
        uint8_t buf[6];
        size_t offset = writeCommandPrefix(buf, CMD_SET_TARGET);
        buf[offset++] = channel & 0x7F;
        write14BitData(buf, offset, target);
        return writeBytes(buf, offset, "set target");
    }

    /**
     * Set Speed (command 0x87).
     * Sends: [0xAA, device], command, channel, speed_low_7bits, speed_high_7bits
     * Pololu protocol command byte is 0x07; compact protocol command byte is 0x87.
     */
    [[nodiscard]] bool maestroSetSpeed(uint8_t channel, uint16_t speed) {
        uint8_t buf[6];
        size_t offset = writeCommandPrefix(buf, CMD_SET_SPEED);
        buf[offset++] = channel & 0x7F;
        write14BitData(buf, offset, speed);
        return writeBytes(buf, offset, "set speed");
    }

    /**
     * Set Acceleration (command 0x89).
     * Sends: [0xAA, device], command, channel, accel_low_7bits, accel_high_7bits
     * Pololu protocol command byte is 0x09; compact protocol command byte is 0x89.
     */
    [[nodiscard]] bool maestroSetAcceleration(uint8_t channel, uint16_t accel) {
        uint8_t buf[6];
        size_t offset = writeCommandPrefix(buf, CMD_SET_ACCELERATION);
        buf[offset++] = channel & 0x7F;
        write14BitData(buf, offset, accel);
        return writeBytes(buf, offset, "set acceleration");
    }

    /**
     * Set Multiple Targets (command 0x9F).
     * Sends: [0xAA, device], command, count, firstChannel, then count target byte pairs.
     * Pololu protocol command byte is 0x1F; compact protocol command byte is 0x9F.
     */
    [[nodiscard]] bool maestroSetMultiTarget(uint8_t count, uint8_t firstChannel, const uint16_t* targets) {
        if (count > kMaxChannels || targets == nullptr) {
            return false;
        }

        uint8_t buf[5 + (kMaxChannels * 2)];
        size_t offset = writeCommandPrefix(buf, CMD_SET_MULTI_TARGET);
        buf[offset++] = count & 0x7F;
        buf[offset++] = firstChannel & 0x7F;

        for (uint8_t i = 0; i < count; i++) {
            write14BitData(buf, offset, targets[i]);
        }
        return writeBytes(buf, offset, "multi target");
    }

    [[nodiscard]] size_t writeCommandPrefix(uint8_t* buf, uint8_t command) const {
        size_t offset = 0;
        if (m_deviceNumber != kCompactProtocolDeviceNumber) {
            buf[offset++] = CMD_POLOLU_START;
            buf[offset++] = m_deviceNumber & 0x7F;
            buf[offset++] = command & 0x7F;
        } else {
            buf[offset++] = command;
        }
        return offset;
    }

    static void write14BitData(uint8_t* buf, size_t& offset, uint16_t value) {
        buf[offset++] = static_cast<uint8_t>(value & 0x7F);
        buf[offset++] = static_cast<uint8_t>((value >> 7) & 0x7F);
    }

    [[nodiscard]] bool writeBytes(const uint8_t* data, size_t length, const char* operation) {
        const size_t written = m_serial->write(data, length);
        if (written == length) {
            return true;
        }
        m_status = DriverStatus::kError;
        m_lastError.set(ERROR_SERIAL_WRITE, 0, "Maestro serial write incomplete");
        ESP_LOGE(m_name, "%s write failed: %u/%u bytes", operation, static_cast<unsigned>(written),
                 static_cast<unsigned>(length));
        return false;
    }

    ISerialPort* m_serial;
    uint8_t m_channelCount;
    const char* m_name;
    uint8_t m_deviceNumber;
    DriverStatus m_status = DriverStatus::kUninitialized;
    ErrorInfo m_lastError;

    uint16_t m_positions[kMaxChannels]{};
    uint16_t m_targets[kMaxChannels]{};
    uint16_t m_previousTargets[kMaxChannels]{};
    bool m_enabled[kMaxChannels]{};
};

}  // namespace chopper::hal
