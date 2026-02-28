#pragma once

#include "chopper/hal/IDriver.h"

namespace chopper {
namespace hal {

/**
 * Servo controller interface for multi-channel PWM servo control.
 *
 * Pulse widths are in microseconds (typically 500-2500us).
 * Angles are in degrees (0-360, depending on actuator range).
 */
class IServoController : public IDriver {
public:
    ~IServoController() override = default;

    /// Set servo position in pulse-width microseconds.
    virtual void setPosition(uint8_t channel, uint16_t pulse_us) = 0;

    /// Set servo position by angle (degrees). Controller maps to pulse width.
    virtual void setAngle(uint8_t channel, float angle) = 0;

    /// Get current position of a channel in pulse-width microseconds.
    virtual uint16_t getPosition(uint8_t channel) const = 0;

    /// Enable PWM output on a channel.
    virtual void enable(uint8_t channel) = 0;

    /// Disable PWM output on a channel (stop sending pulses).
    virtual void disable(uint8_t channel) = 0;

    /// Disable all PWM outputs.
    virtual void disableAll() = 0;

    /// Set speed limit for a channel (controller-specific units).
    virtual void setSpeed(uint8_t channel, uint16_t speed) = 0;

    /// Set acceleration limit for a channel (controller-specific units).
    virtual void setAcceleration(uint8_t channel, uint16_t accel) = 0;

    /// Get the number of servo channels this controller supports.
    virtual uint8_t getChannelCount() const = 0;
};

} // namespace hal
} // namespace chopper
