#pragma once

#include "chopper/hal/ISensorDriver.h"
#include "esp_log.h"
#include <cstring>

namespace chopper {
namespace hal {

/**
 * HAL sensor driver that wraps the dome potentiometer (analog position sensor).
 *
 * Reads the raw ADC value via AnalogMonitor-style filtering and maps it
 * to a 0-359 degree dome angle. The calibration range (min/max ADC values)
 * determines the mapping endpoints.
 *
 * On ESP32, this wraps a DomeSensorAnalogPositionProvider. For host-side
 * testing, use MockSensorDriver instead.
 */
class DomeSensorDriver : public ISensorDriver {
public:
    /**
     * @param pin           ADC GPIO pin number.
     * @param name          Driver name for diagnostics.
     * @param calMin        Raw ADC value corresponding to 0 degrees.
     * @param calMax        Raw ADC value corresponding to 359 degrees.
     */
    DomeSensorDriver(uint8_t pin, const char* name,
                     int32_t calMin = 1225, int32_t calMax = 2500)
        : m_pin(pin)
        , m_name(name)
        , m_calMin(calMin)
        , m_calMax(calMax)
    {
    }

    // -- IDriver interface --

    DriverStatus init() override {
        ESP_LOGI(m_name, "init dome sensor on pin %d (cal %ld-%ld)",
                 m_pin, (long)m_calMin, (long)m_calMax);
        // On real hardware, this would call pinMode and configure ADC
        initHardware();
        m_status = DriverStatus::kReady;
        return m_status;
    }

    void update() override {
        if (m_status != DriverStatus::kReady && m_status != DriverStatus::kDegraded) {
            return;
        }
        int32_t newRaw = readAdc();
        m_changed = (newRaw != m_rawValue);
        m_rawValue = newRaw;
    }

    DriverStatus getStatus() const override { return m_status; }
    ErrorInfo getErrorState() const override { return m_lastError; }
    const char* getName() const override { return m_name; }

    DriverStatus reset() override {
        m_rawValue = 0;
        m_changed = false;
        m_lastError.clear();
        m_status = DriverStatus::kReady;
        return m_status;
    }

    void shutdown() override {
        m_status = DriverStatus::kDisabled;
    }

    // -- ISensorDriver interface --

    int32_t read() const override {
        return m_rawValue;
    }

    float readScaled() const override {
        // Map raw ADC to 0-359 degrees
        if (m_calMax == m_calMin) return 0.0f;
        int32_t clamped = m_rawValue;
        if (clamped < m_calMin) clamped = m_calMin;
        if (clamped > m_calMax) clamped = m_calMax;
        return static_cast<float>(clamped - m_calMin) * 359.0f
             / static_cast<float>(m_calMax - m_calMin);
    }

    bool hasChanged() const override {
        return m_changed;
    }

    void setCalibration(int32_t min, int32_t max) override {
        m_calMin = min;
        m_calMax = max;
        ESP_LOGI(m_name, "calibration updated: %ld-%ld", (long)min, (long)max);
    }

    /// Get the dome angle in degrees (0-359).
    int getAngle() const {
        float scaled = readScaled();
        int angle = static_cast<int>(scaled);
        if (angle < 0) angle = 0;
        if (angle > 359) angle = 359;
        return angle;
    }

    bool handleDiagnostic(const char* command, char* response, size_t maxLen) override {
        if (!command || !response || maxLen == 0) return false;

        if (strcmp(command, "status") == 0) {
            snprintf(response, maxLen, "%s, raw=%ld, angle=%d",
                     driverStatusToString(m_status), (long)m_rawValue, getAngle());
            return true;
        }
        return false;
    }

private:
    /// Hardware abstraction — reads the ADC pin. On host, returns 0.
    int32_t readAdc();

    /// Configure the GPIO pin for analog input.
    void initHardware();

    uint8_t m_pin;
    const char* m_name;
    int32_t m_calMin;
    int32_t m_calMax;
    int32_t m_rawValue = 0;
    bool m_changed = false;
    DriverStatus m_status = DriverStatus::kUninitialized;
    ErrorInfo m_lastError;
};

} // namespace hal
} // namespace chopper
