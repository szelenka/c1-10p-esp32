#pragma once

#include "chopper/hal/IDriver.h"

namespace chopper {
namespace hal {

/**
 * Sensor driver interface for analog/digital sensor inputs.
 */
class ISensorDriver : public IDriver {
public:
    ~ISensorDriver() override = default;

    /// Read the raw (filtered) sensor value.
    virtual int32_t read() const = 0;

    /// Read a normalized/calibrated value (0.0 .. 1.0 or application-specific).
    virtual float readScaled() const = 0;

    /// Returns true if the value changed since the last update() cycle.
    virtual bool hasChanged() const = 0;

    /// Set calibration range for scaling.
    virtual void setCalibration(int32_t min, int32_t max) = 0;
};

}  // namespace hal
}  // namespace chopper
