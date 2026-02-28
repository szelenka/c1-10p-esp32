#pragma once

#include <algorithm>
#include <cstdint>

namespace chopper {
namespace math {

/**
 * Limits the rate of change of an input value.
 *
 * Useful for implementing voltage, setpoint, and/or output ramps.
 * Accepts an explicit timestamp so callers can inject time for testing.
 *
 * Rate limits are in units-per-second.  The Calculate() overload that
 * takes a timestamp expects milliseconds (uint64_t).
 */
class SlewRateLimiter {
public:
    /**
     * @param positiveRateLimit  Rate limit in positive direction (units/sec, positive).
     * @param negativeRateLimit  Rate limit in negative direction (units/sec, negative).
     * @param initialValue       Starting value.
     */
    SlewRateLimiter(float positiveRateLimit, float negativeRateLimit,
                    float initialValue = 0.0f)
        : m_positiveRateLimit(positiveRateLimit)
        , m_negativeRateLimit(negativeRateLimit)
        , m_prevVal(initialValue)
        , m_prevTime(0)
        , m_seeded(false)
    {}

    /**
     * Symmetric rate limit (positive = rateLimit, negative = -rateLimit).
     */
    explicit SlewRateLimiter(float rateLimit)
        : SlewRateLimiter(rateLimit, -rateLimit)
    {}

    /**
     * Filter the input.  Caller provides current time in milliseconds.
     */
    float Calculate(float input, uint64_t now_ms) {
        if (!m_seeded) {
            // First call — seed the time, return input as-is.
            m_prevTime = now_ms;
            m_prevVal = input;
            m_seeded = true;
            return m_prevVal;
        }
        float elapsed = static_cast<float>(now_ms - m_prevTime);
        m_prevVal += std::clamp(
            input - m_prevVal,
            m_negativeRateLimit * elapsed / 1000.0f,
            m_positiveRateLimit * elapsed / 1000.0f);
        m_prevTime = now_ms;
        return m_prevVal;
    }

    float LastValue() const { return m_prevVal; }

    void Reset(float positiveRateLimit, float negativeRateLimit,
               float initialValue = 0.0f) {
        m_positiveRateLimit = positiveRateLimit;
        m_negativeRateLimit = negativeRateLimit;
        m_prevVal = initialValue;
        m_prevTime = 0;
        m_seeded = false;
    }

    void Reset(float initialValue = 0.0f) {
        m_prevVal = initialValue;
        m_prevTime = 0;
        m_seeded = false;
    }

private:
    float m_positiveRateLimit;
    float m_negativeRateLimit;
    float m_prevVal;
    uint64_t m_prevTime;
    bool m_seeded;
};

} // namespace math
} // namespace chopper
