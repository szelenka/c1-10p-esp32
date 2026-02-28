#pragma once

#include "chopper/math/AnalogFilter.h"
#include "chopper/dome/DomePosition.h"
#include <algorithm>

namespace chopper {
namespace dome {

/**
 * Reads a potentiometer (via raw ADC values) and feeds DomePosition.
 *
 * Replaces the old DomeSensorAnalogPositionProvider + AnalogMonitor chain.
 * Framework-free: caller provides raw ADC readings and timestamps.
 * The actual ADC read (e.g. adc1_get_raw()) is the caller's responsibility.
 *
 * Usage:
 *   DomePosition pos;
 *   DomePotentiometer pot(&pos);
 *   // In your sensor read loop:
 *   int raw = adc1_get_raw(ADC1_CHANNEL_6);  // GPIO34
 *   pot.update(raw, now_ms);
 *   // pos is now updated with the filtered angle
 */
class DomePotentiometer {
public:
    /**
     * @param dome_position  DomePosition to update (externally owned).
     * @param adc_min        Raw ADC value at 0 degrees.
     * @param adc_max        Raw ADC value at 359 degrees.
     * @param resolution     ADC resolution (4096 for ESP32 12-bit).
     */
    DomePotentiometer(DomePosition* dome_position,
                      int adc_min = 1225, int adc_max = 2500,
                      int resolution = 4096)
        : dome_position_(dome_position)
        , adc_min_(adc_min)
        , adc_max_(adc_max)
        , filter_(resolution)
    {}

    /**
     * Feed a raw ADC reading and update DomePosition with the filtered angle.
     * @param raw_adc  Raw ADC value from the potentiometer.
     * @param now_ms   Current time in milliseconds.
     * @return The computed angle (0-359).
     */
    int update(int raw_adc, uint64_t now_ms) {
        int filtered = filter_.update(raw_adc);
        int angle = mapToAngle(filtered);
        if (dome_position_) {
            dome_position_->update(static_cast<unsigned>(angle), now_ms);
        }
        return angle;
    }

    /** Get the last computed angle without updating. */
    int getLastAngle() const { return last_angle_; }

    /** Check if the filtered value changed on the last update. */
    bool hasChanged() const { return filter_.hasChanged(); }

    /** Access the underlying filter for tuning. */
    math::AnalogFilter& getFilter() { return filter_; }

    /** Set ADC-to-angle calibration range. */
    void setCalibration(int adc_min, int adc_max) {
        adc_min_ = adc_min;
        adc_max_ = adc_max;
    }

private:
    int mapToAngle(int filtered) {
        // Clamp to calibration range
        int clamped = std::clamp(filtered, adc_min_, adc_max_);
        // Linear map: [adc_min_, adc_max_] → [0, 359]
        int angle = static_cast<int>(
            static_cast<long>(clamped - adc_min_) * 359
            / std::max(adc_max_ - adc_min_, 1));
        last_angle_ = angle;
        return angle;
    }

    DomePosition* dome_position_;
    int adc_min_;
    int adc_max_;
    int last_angle_ = 0;
    math::AnalogFilter filter_;
};

} // namespace dome
} // namespace chopper
