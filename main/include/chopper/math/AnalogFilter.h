#pragma once

#include <cmath>
#include <cstdint>

namespace chopper {
namespace math {

/**
 * Noise-filtering algorithm for analog sensor readings.
 *
 * Ported from Damien Clarke's ResponsiveAnalogRead / AnalogMonitor.
 * Framework-free: caller provides raw ADC values, this class applies
 * exponential smoothing with a snap curve and optional sleep mode.
 *
 * - Small noise around a stable value is aggressively filtered.
 * - Medium/large movements snap responsively.
 * - Sleep mode stops output changes quickly when input stabilizes.
 * - Edge snap makes it easier to reach 0 and max values.
 */
class AnalogFilter {
public:
    /**
     * @param resolution       ADC resolution (e.g. 4096 for 12-bit ESP32).
     * @param sleep_enable     Enable sleep mode for faster settling.
     * @param snap_multiplier  0..1, higher = more responsive but noisier.
     */
    AnalogFilter(int resolution = 4096, bool sleep_enable = true,
                 float snap_multiplier = 0.01f)
        : fAnalogResolution(resolution)
        , fSleepEnable(sleep_enable)
        , fEdgeSnapEnable(true)
        , fActivityThreshold(4.0f)
        , fSmoothValue(0.0f)
        , fErrorEMA(0.0f)
        , fSleeping(false)
        , fResponsiveValue(0)
        , fPrevResponsiveValue(0)
        , fHasChanged(false)
        , fSeeded(false)
    {
        setSnapMultiplier(snap_multiplier);
    }

    /**
     * Feed a new raw ADC reading and compute the filtered output.
     * @param raw_value  Raw ADC reading (0..resolution-1).
     * @return Filtered value.
     */
    int update(int raw_value) {
        if (!fSeeded) {
            fSmoothValue = static_cast<float>(raw_value);
            fSeeded = true;
        }
        fPrevResponsiveValue = fResponsiveValue;
        fResponsiveValue = computeResponsive(raw_value);
        fHasChanged = (fResponsiveValue != fPrevResponsiveValue);
        return fResponsiveValue;
    }

    int getValue() const { return fResponsiveValue; }
    bool hasChanged() const { return fHasChanged; }
    bool isSleeping() const { return fSleeping; }

    void setSnapMultiplier(float m) {
        fSnapMultiplier = (m > 1.0f) ? 1.0f : (m < 0.0f) ? 0.0f : m;
    }

    void enableSleep() { fSleepEnable = true; }
    void disableSleep() { fSleepEnable = false; }
    void enableEdgeSnap() { fEdgeSnapEnable = true; }
    void disableEdgeSnap() { fEdgeSnapEnable = false; }
    void setActivityThreshold(float t) { fActivityThreshold = t; }
    void setAnalogResolution(int r) { fAnalogResolution = r; }

private:
    int computeResponsive(int newValue) {
        // Edge snap: drag values near edges closer to the extremes
        if (fSleepEnable && fEdgeSnapEnable) {
            if (newValue < fActivityThreshold) {
                newValue = static_cast<int>(newValue * 2 - fActivityThreshold);
            } else if (newValue > fAnalogResolution - fActivityThreshold) {
                newValue = static_cast<int>(newValue * 2 - fAnalogResolution
                                            + fActivityThreshold);
            }
        }

        float diff = std::fabs(static_cast<float>(newValue) - fSmoothValue);

        // EMA of error for sleep detection
        fErrorEMA += ((newValue - fSmoothValue) - fErrorEMA) * 0.4f;

        if (fSleepEnable) {
            fSleeping = std::fabs(fErrorEMA) < fActivityThreshold;
        }

        // If sleeping, hold current value
        if (fSleepEnable && fSleeping) {
            return static_cast<int>(fSmoothValue);
        }

        // Snap curve: small diff → slow response, large diff → fast response
        float snap = snapCurve(diff * fSnapMultiplier);

        if (fSleepEnable) {
            snap = snap * 0.5f + 0.5f;
        }

        // Exponential moving average
        fSmoothValue += (newValue - fSmoothValue) * snap;

        // Clamp
        if (fSmoothValue < 0.0f) {
            fSmoothValue = 0.0f;
        } else if (fSmoothValue > fAnalogResolution - 1) {
            fSmoothValue = static_cast<float>(fAnalogResolution - 1);
        }

        return static_cast<int>(fSmoothValue);
    }

    static float snapCurve(float x) {
        float y = 1.0f / (x + 1.0f);
        y = (1.0f - y) * 2.0f;
        return (y > 1.0f) ? 1.0f : y;
    }

    int fAnalogResolution;
    float fSnapMultiplier = 0.01f;
    bool fSleepEnable;
    float fActivityThreshold;
    bool fEdgeSnapEnable;

    float fSmoothValue;
    float fErrorEMA;
    bool fSleeping;

    int fResponsiveValue;
    int fPrevResponsiveValue;
    bool fHasChanged;
    bool fSeeded;
};

} // namespace math
} // namespace chopper
