// Copyright (c) FIRST and other WPILib contributors.
// Open Source Software; you can modify and/or share it under the terms of
// the WPILib BSD license file in the root directory of this project.

#pragma once

#include <algorithm>
#include <Bluepad32.h>

#include "chopper/Timer.h"

/**
 * A class that limits the rate of change of an input value.  Useful for
 * implementing voltage, setpoint, and/or output ramps.  A slew-rate limit
 * is most appropriate when the quantity being controlled is a velocity or
 * a voltage; when controlling a position, consider using a TrapezoidProfile
 * instead.
 *
 * @see TrapezoidProfile
 */
class SlewRateLimiter {
 public:

  /**
   * Creates a new SlewRateLimiter with the given positive and negative rate
   * limits and initial value.
   *
   * @param positiveRateLimit The rate-of-change limit in the positive
   *                          direction, in units per second. This is expected
   *                          to be positive.
   * @param negativeRateLimit The rate-of-change limit in the negative
   *                          direction, in units per second. This is expected
   *                          to be negative.
   * @param initialValue The initial value of the input.
   */
  SlewRateLimiter(double positiveRateLimit, double negativeRateLimit,
                  double initialValue = 0.0)
      : m_positiveRateLimit{positiveRateLimit},
        m_negativeRateLimit{negativeRateLimit},
        m_prevVal{initialValue},
        m_prevTime{Timer::GetFPGATimestamp()} {}

  /**
   * Creates a new SlewRateLimiter with the given positive rate limit and
   * negative rate limit of -rateLimit.
   *
   * @param rateLimit The rate-of-change limit.
   */
  explicit SlewRateLimiter(double rateLimit)
      : SlewRateLimiter(rateLimit, -rateLimit) {}

  /**
   * Filters the input to limit its slew rate.
   *
   * @param input The input value whose slew rate is to be limited.
   * @return The filtered value, which will not change faster than the slew
   * rate.
   */
  double Calculate(double input) {
    uint64_t currentTime = Timer::GetFPGATimestamp();
    uint64_t elapsedTime = currentTime - m_prevTime;
    // smooth out the rateLimit across milliseconds to get the per-second rateLimit
    m_prevVal +=
        std::clamp(
            input - m_prevVal, 
            m_negativeRateLimit * elapsedTime / 1000,
            m_positiveRateLimit * elapsedTime / 1000);
    m_prevTime = currentTime;
    return m_prevVal;
  }

  /**
   * Returns the value last calculated by the SlewRateLimiter.
   *
   * @return The last value.
   */
  double LastValue() const { return m_prevVal; }

  /**
   * Resets the slew rate limiter to the specified value; ignores the rate limit
   * when doing so.
   *
   * @param positiveRateLimit The rate-of-change limit in the positive
   *                          direction, in units per second. This is expected
   *                          to be positive.
   * @param negativeRateLimit The rate-of-change limit in the negative
   *                          direction, in units per second. This is expected
   *                          to be negative.
   * @param initialValue The initial value of the input.
   */
  void Reset(double positiveRateLimit, double negativeRateLimit,
                  double initialValue = 0.0)
  {
    m_positiveRateLimit = positiveRateLimit;
    m_negativeRateLimit = negativeRateLimit;
    m_prevVal = initialValue;
    m_prevTime = Timer::GetFPGATimestamp();
  }

 private:
  double m_positiveRateLimit;
  double m_negativeRateLimit;
  double m_prevVal;
  uint64_t m_prevTime;
};
