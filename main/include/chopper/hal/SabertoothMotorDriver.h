#pragma once

#include "chopper/hal/IMotorDriver.h"
#include "chopper/hal/ISerialPort.h"
#include "esp_log.h"
#include <cstring>

namespace chopper::hal {

/**
 * HAL motor driver that wraps a single motor channel on a Sabertooth/SyRen
 * motor controller using the Packet Serial protocol.
 *
 * Implements the Dimension Engineering Packet Serial protocol directly
 * via ISerialPort, removing the Arduino Sabertooth library dependency.
 *
 * Protocol reference: https://www.dimensionengineering.com/datasheets/Sabertooth2x32.pdf
 *   - 4-byte packets: [address, command, value, checksum]
 *   - Checksum = (address + command + value) & 0x7F
 *   - Motor 1: forward=0, reverse=1
 *   - Motor 2: forward=4, reverse=5
 *
 * One instance per physical motor channel. Multiple SabertoothMotorDriver
 * instances can share the same ISerialPort (which represents the shared
 * UART bus to the controller).
 */
class SabertoothMotorDriver : public IMotorDriver {
public:
    // Packet Serial command bytes
    static constexpr uint8_t CMD_MOTOR1_FORWARD = 0;
    static constexpr uint8_t CMD_MOTOR1_REVERSE = 1;
    static constexpr uint8_t CMD_MOTOR2_FORWARD = 4;
    static constexpr uint8_t CMD_MOTOR2_REVERSE = 5;
    static constexpr uint8_t CMD_SET_TIMEOUT = 14;
    static constexpr uint8_t CMD_SET_RAMPING = 16;
    static constexpr uint8_t CMD_SET_DEADBAND = 17;

    static constexpr uint8_t AUTOBAUD_BYTE = 0xAA;

    /**
     * @param serial    Serial port for Sabertooth communication (shared bus).
     * @param address   Packet Serial address of the controller (128-135).
     * @param motorId   Motor number on the controller (1 or 2).
     * @param name      Driver name for diagnostics (must be string literal / static).
     */
    SabertoothMotorDriver(ISerialPort& serial, uint8_t address, uint8_t motorId, const char* name)
        : m_serial(&serial), m_address(address), m_motorId(motorId), m_name(name) {}

    // -- IDriver interface --

    DriverStatus init() override {
        ESP_LOGI(m_name, "init motor %d addr %d", m_motorId, m_address);
        sendAutobaud();
        m_speed = 0.0f;
        m_status = DriverStatus::kReady;
        return m_status;
    }

    void update() override {
        if (m_status != DriverStatus::kReady && m_status != DriverStatus::kDegraded) {
            return;
        }
        // Apply speed to hardware — Sabertooth expects -127..127 for motor()
        float effective = m_inverted ? -m_speed : m_speed;
        int power = static_cast<int>(effective * 127.0f);
        // Clamp to [-126, 126] (matches Sabertooth library behavior)
        if (power > 126) {
            power = 126;
        }
        if (power < -126) {
            power = -126;
        }
        sabertoothMotor(m_motorId, power);
    }

    [[nodiscard]] DriverStatus getStatus() const override { return m_status; }

    [[nodiscard]] ErrorInfo getErrorState() const override { return m_lastError; }

    [[nodiscard]] const char* getName() const override { return m_name; }

    DriverStatus reset() override {
        m_speed = 0.0f;
        m_lastError.clear();
        m_status = DriverStatus::kReady;
        return m_status;
    }

    void shutdown() override {
        m_speed = 0.0f;
        sabertoothMotor(m_motorId, 0);
        m_status = DriverStatus::kDisabled;
    }

    // -- IMotorDriver interface --

    void set(float speed) override {
        // Clamp to [-1.0, 1.0]
        if (speed > 1.0f) {
            speed = 1.0f;
        }
        if (speed < -1.0f) {
            speed = -1.0f;
        }
        m_speed = speed;
    }

    [[nodiscard]] float get() const override { return m_speed; }

    void setInverted(bool inverted) override { m_inverted = inverted; }
    [[nodiscard]] bool isInverted() const override { return m_inverted; }

    void disable() override {
        m_speed = 0.0f;
        sabertoothMotor(m_motorId, 0);
    }

    void stop() override {
        m_speed = 0.0f;
        sabertoothMotor(m_motorId, 0);
    }

    bool handleDiagnostic(const char* command, char* response, size_t maxLen) override {
        if ((command == nullptr) || (response == nullptr) || maxLen == 0) {
            return false;
        }

        if (strcmp(command, "status") == 0) {
            snprintf(response, maxLen, "%s, speed=%.2f, inverted=%s", driverStatusToString(m_status), m_speed,
                     m_inverted ? "true" : "false");
            return true;
        }
        if (strcmp(command, "stop") == 0) {
            stop();
            snprintf(response, maxLen, "stopped");
            return true;
        }
        return false;
    }

    // -- Configuration methods (call after init) --

    void setTimeout(uint8_t value) { sendCommand(CMD_SET_TIMEOUT, value); }
    void setRamping(uint8_t value) { sendCommand(CMD_SET_RAMPING, value); }
    void setDeadband(uint8_t value) { sendCommand(CMD_SET_DEADBAND, value); }

    // -- Test / inspection accessors --

    [[nodiscard]] uint8_t getAddress() const { return m_address; }
    [[nodiscard]] uint8_t getMotorId() const { return m_motorId; }

private:
    /**
     * Send the autobaud byte (0xAA) to synchronize the Sabertooth.
     * Must be sent once after power-up before any commands.
     */
    void sendAutobaud() {
        uint8_t byte = AUTOBAUD_BYTE;
        m_serial->write(&byte, 1);
    }

    /**
     * Send a Packet Serial command.
     * Format: [address, command, value, checksum]
     * Checksum = (address + command + value) & 0x7F
     */
    void sendCommand(uint8_t cmd, uint8_t value) {
        uint8_t buf[4];
        buf[0] = m_address;
        buf[1] = cmd;
        buf[2] = value;
        buf[3] = (m_address + cmd + value) & 0x7F;
        m_serial->write(buf, 4);
    }

    /**
     * Send motor command with signed power value.
     * Maps motor number (1/2) and sign to the appropriate command byte.
     *
     * Motor 1: forward=cmd 0, reverse=cmd 1
     * Motor 2: forward=cmd 4, reverse=cmd 5
     */
    void sabertoothMotor(uint8_t motor, int power) {
        uint8_t cmd = 0;
        uint8_t absValue = 0;

        if (power >= 0) {
            cmd = (motor == 1) ? CMD_MOTOR1_FORWARD : CMD_MOTOR2_FORWARD;
            absValue = static_cast<uint8_t>(power);
        } else {
            cmd = (motor == 1) ? CMD_MOTOR1_REVERSE : CMD_MOTOR2_REVERSE;
            absValue = static_cast<uint8_t>(-power);
        }

        sendCommand(cmd, absValue);
    }

    ISerialPort* m_serial;
    uint8_t m_address;
    uint8_t m_motorId;
    const char* m_name;
    float m_speed = 0.0f;
    bool m_inverted = false;
    DriverStatus m_status = DriverStatus::kUninitialized;
    ErrorInfo m_lastError;
};

}  // namespace chopper::hal
