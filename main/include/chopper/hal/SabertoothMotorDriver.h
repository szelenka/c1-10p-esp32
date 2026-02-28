#pragma once

#include "chopper/hal/IMotorDriver.h"
#include "esp_log.h"

// Forward-declare the Sabertooth library class to avoid pulling in the
// full header in every translation unit.
class Sabertooth;

namespace chopper {
namespace hal {

/**
 * HAL motor driver that wraps a single motor channel on a Sabertooth/SyRen
 * motor controller using the Packet Serial protocol.
 *
 * One instance per physical motor channel. Multiple SabertoothMotorDriver
 * instances can share the same Sabertooth library object (which represents
 * the controller at a given address on a shared UART bus).
 */
class SabertoothMotorDriver : public IMotorDriver {
public:
    /**
     * @param sabertooth  Reference to the Sabertooth library object (shared bus).
     * @param motorId     Motor number on the controller (1 or 2).
     * @param name        Driver name for diagnostics (must be string literal / static).
     */
    SabertoothMotorDriver(Sabertooth& sabertooth, uint8_t motorId, const char* name)
        : m_sabertooth(&sabertooth)
        , m_motorId(motorId)
        , m_name(name)
    {
    }

    // -- IDriver interface --

    DriverStatus init() override {
        ESP_LOGI(m_name, "init motor %d", m_motorId);
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
        // Clamp
        if (power > 127) power = 127;
        if (power < -127) power = -127;
        sabertoothMotor(m_motorId, power);
    }

    DriverStatus getStatus() const override { return m_status; }

    ErrorInfo getErrorState() const override { return m_lastError; }

    const char* getName() const override { return m_name; }

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
        if (speed > 1.0f) speed = 1.0f;
        if (speed < -1.0f) speed = -1.0f;
        m_speed = speed;
    }

    float get() const override { return m_speed; }

    void setInverted(bool inverted) override { m_inverted = inverted; }
    bool isInverted() const override { return m_inverted; }

    void disable() override {
        m_speed = 0.0f;
        sabertoothMotor(m_motorId, 0);
    }

    void stop() override {
        m_speed = 0.0f;
        sabertoothStop(m_motorId);
    }

    bool handleDiagnostic(const char* command, char* response, size_t maxLen) override {
        if (!command || !response || maxLen == 0) return false;

        if (strcmp(command, "status") == 0) {
            snprintf(response, maxLen, "%s, speed=%.2f, inverted=%s",
                     driverStatusToString(m_status), m_speed,
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

private:
    /// Send motor command to Sabertooth hardware.
    /// Separated to allow the concrete Sabertooth calls to live in one place.
    void sabertoothMotor(uint8_t motor, int power);

    /// Send stop command (active brake) to Sabertooth hardware.
    void sabertoothStop(uint8_t motor);

    Sabertooth* m_sabertooth;
    uint8_t m_motorId;
    const char* m_name;
    float m_speed = 0.0f;
    bool m_inverted = false;
    DriverStatus m_status = DriverStatus::kUninitialized;
    ErrorInfo m_lastError;
};

} // namespace hal
} // namespace chopper
