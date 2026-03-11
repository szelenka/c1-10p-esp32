#pragma once

#include <algorithm>
#include <cmath>
#include <type_traits>

namespace chopper::math {

/**
 * Returns 0 if the value is within the deadband around zero.
 * The remaining range is scaled from 0 to maxMagnitude.
 */
template <typename T>
typename std::enable_if<std::is_arithmetic<T>::value, T>::type ApplyDeadband(T value, T deadband,
                                                                             T maxMagnitude = T{1.0}) {
    T magnitude = std::abs(value);

    if (magnitude > deadband) {
        if (maxMagnitude / deadband > T{1.0E12}) {
            return value > T{0.0} ? value - deadband : value + deadband;
        }
        if (value > T{0.0}) {
            return maxMagnitude * (value - deadband) / (maxMagnitude - deadband);
        }
        return maxMagnitude * (value + deadband) / (maxMagnitude - deadband);
    }
    return T{0.0};
}

/**
 * Limits the speed to a specified range.
 */
template <typename T>
typename std::enable_if<std::is_arithmetic<T>::value, T>::type ApplySpeedLimit(T speed, T speedLimit) {
    T magnitude = std::clamp(std::abs(speed), T{0.0}, T{1.0});
    T limited = std::clamp(std::abs(speedLimit), T{0.0}, T{1.0});

    if (magnitude > limited) {
        return std::copysign(limited, speed);
    }
    return speed;
}

/**
 * Arduino-compatible map() for integer ranges.
 * Maps value from [in_min, in_max] to [out_min, out_max].
 */
inline long mapValue(long value, long in_min, long in_max, long out_min, long out_max) {
    return (value - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

}  // namespace chopper::math
