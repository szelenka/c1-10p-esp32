#pragma once

#include "chopper/hal/IDriver.h"

namespace chopper::hal {

/**
 * Motor driver interface for DC motor control.
 *
 * Speed values are normalized to [-1.0 .. 1.0] where:
 *   -1.0 = full reverse
 *    0.0 = stopped
 *   +1.0 = full forward
 */
class IMotorDriver : public IDriver {
public:
    ~IMotorDriver() override = default;

    /// Set motor speed [-1.0 .. 1.0]. Clamped internally.
    virtual void set(float speed) = 0;

    /// Get current speed setpoint.
    [[nodiscard]] virtual float get() const = 0;

    /// Invert the motor direction.
    virtual void setInverted(bool inverted) = 0;

    /// Check if motor direction is inverted.
    [[nodiscard]] virtual bool isInverted() const = 0;

    /// Coast / Hi-Z — motor free-spins.
    virtual void disable() = 0;

    /// Active brake — motor actively resists motion.
    virtual void stop() = 0;
};

}  // namespace chopper::hal
