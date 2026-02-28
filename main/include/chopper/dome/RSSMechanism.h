#pragma once

#include "chopper/math/RSSMachine.h"
#include "chopper/math/MathUtil.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <tuple>

namespace chopper {
namespace dome {

/**
 * Higher-level wrapper over RSSMachine that adds:
 *   - Joystick-to-servo-angle and joystick-to-PWM pipelines
 *   - Per-leg PWM offset calibration
 *   - Rotation angle offset (align joystick "up" with physical triangle)
 *   - Enable/disable with debounce
 *   - Height increment/decrement controls
 *
 * Replaces old global RSSMechanism class; now framework-free (no Timer dep).
 * Callers must pass timestamps explicitly.
 */
class RSSMechanism : public math::RSSMachine {
public:
    RSSMechanism(float base_altitude, float end_effector_altitude,
                 float bottom_link_length, float top_link_length,
                 float min_height, float limit_normal_vector, bool bend_out)
        : RSSMachine(base_altitude, end_effector_altitude,
                     bottom_link_length, top_link_length,
                     min_height, limit_normal_vector, bend_out)
    {
        _platformCurrentHeight = (_platformMinHeight + _platformMaxHeight) / 2.0f;
        _platformPreviousHeight = _platformMinHeight;
    }

    ~RSSMechanism() = default;

    bool isEnabled() const { return _isEnabled; }

    /**
     * Enable/disable with debounce.  Caller provides current time in ms.
     */
    void setEnabled(bool enabled, uint64_t now_ms) {
        if (_hasBeenToggled && now_ms - _lastEnabledChange < _debounceTimeout)
            return;
        _hasBeenToggled = true;
        _isEnabled = enabled;
        _lastEnabledChange = now_ms;
        if (_isEnabled) {
            setHeight(_platformMaxHeight - 5.0f);
        } else {
            setHeight(_platformMinHeight);
        }
    }

    void setRotationAngleOffset(float angle) {
        _rotationRadianOffset = std::clamp(angle, 0.0f, 160.0f)
                                * (static_cast<float>(M_PI) / 180.0f);
    }

    void setActuationRange(uint16_t actuationRange) {
        switch (actuationRange) {
            case 270:
                _servoTheoreticalMinPulse = 500;
                _servoTheoreticalMaxPulse = 2500;
                break;
            default:
                _servoTheoreticalMinPulse = 750;
                _servoTheoreticalMaxPulse = 2250;
                break;
        }
        _servoActuationRange = actuationRange;
    }

    void setLegMinPulse(uint16_t minA, uint16_t minB, uint16_t minC) {
        _servoMinPulse[0] = minA;
        _servoMinPulse[1] = minB;
        _servoMinPulse[2] = minC;
        _referenceMinPWM = static_cast<uint16_t>(
            std::round((static_cast<float>(minA) + minB + minC) / 3.0f));
        calculateLegOffsets();
    }

    void setLegMaxPulse(uint16_t maxA, uint16_t maxB, uint16_t maxC) {
        _servoMaxPulse[0] = maxA;
        _servoMaxPulse[1] = maxB;
        _servoMaxPulse[2] = maxC;
        _referenceMaxPWM = static_cast<uint16_t>(
            std::round((static_cast<float>(maxA) + maxB + maxC) / 3.0f));
        calculateLegOffsets();
    }

    void incrementHeight(float increment) {
        _platformPreviousHeight = _platformCurrentHeight;
        _platformCurrentHeight = std::clamp(
            _platformCurrentHeight + increment, _platformMinHeight, _platformMaxHeight);
    }

    void decrementHeight(float decrement) {
        _platformPreviousHeight = _platformCurrentHeight;
        _platformCurrentHeight = std::clamp(
            _platformCurrentHeight - decrement, _platformMinHeight, _platformMaxHeight);
    }

    void setHeight(float height) {
        _platformPreviousHeight = _platformCurrentHeight;
        _platformCurrentHeight = std::clamp(height, _platformMinHeight, _platformMaxHeight);
    }

    float getCurrentHeight() const { return _platformCurrentHeight; }

    void calculateLegOffsets() {
        uint16_t platformMinHeightPWM = mapAngleToPWM(_platformMinHeightAngle);
        uint16_t platformMaxHeightPWM = mapAngleToPWM(_platformMaxHeightAngle);
        uint16_t offset = 0;

        for (size_t i = 0; i < 3; ++i) {
            if (_servoMinPulse[i] != 0 && _servoMaxPulse[i] == 0) {
                offset = std::max(_referenceMinPWM, _referenceMaxPWM)
                       - std::max(platformMinHeightPWM, platformMaxHeightPWM);
                _servoOffsetPWM[i] = offset + (_servoMinPulse[i] - _referenceMinPWM);
            } else if (_servoMinPulse[i] == 0 || _servoMaxPulse[i] != 0) {
                offset = std::min(_referenceMinPWM, _referenceMaxPWM)
                       - std::min(platformMinHeightPWM, platformMaxHeightPWM);
                _servoOffsetPWM[i] = offset + (_servoMaxPulse[i] - _referenceMaxPWM);
            } else {
                offset = (
                    std::max(_referenceMinPWM, _referenceMaxPWM)
                    - std::max(platformMinHeightPWM, platformMaxHeightPWM)
                    + std::min(_referenceMinPWM, _referenceMaxPWM)
                    - std::min(platformMinHeightPWM, platformMaxHeightPWM)
                ) / 2;
                _servoOffsetPWM[i] = offset + (
                    (_servoMinPulse[i] - _referenceMinPWM)
                    + (_servoMaxPulse[i] - _referenceMaxPWM)
                ) / 2;
            }
            _servoOffsetAngle[i] = mapPWMToAngle(_servoOffsetPWM[i]);
        }
    }

    std::tuple<float, float> adjustJoystickToAngleOffset(float& x, float& y) {
        x = std::round(math::ApplyDeadband(x, m_deadband) * 100.0f) / 100.0f;
        y = std::round(math::ApplyDeadband(y, m_deadband) * 100.0f) / 100.0f;
        float rotatedX = x * std::cos(_rotationRadianOffset)
                       - y * std::sin(_rotationRadianOffset);
        float rotatedY = x * std::sin(_rotationRadianOffset)
                       + y * std::cos(_rotationRadianOffset);
        return std::make_tuple(rotatedX, rotatedY);
    }

    /**
     * Compute servo angles from joystick input.
     * Returns {0, 0, 0} if disabled (past debounce window).
     */
    std::array<float, 3> getLegAnglesFromJoystick(float x, float y, uint64_t now_ms) {
        if (!_isEnabled) {
            if (now_ms - _lastEnabledChange < _debounceTimeout) {
                x = 0.0f;
                y = 0.0f;
            } else {
                return {0.0f, 0.0f, 0.0f};
            }
        }
        std::tie(x, y) = adjustJoystickToAngleOffset(x, y);
        std::array<float, 3> legs = getLegAngles(x, y, _platformCurrentHeight);
        for (size_t i = 0; i < legs.size(); ++i) {
            legs[i] = std::round(legs[i] + _servoOffsetAngle[i]);
        }
        return legs;
    }

    /**
     * Compute servo PWM values from joystick input.
     * Returns {0, 0, 0} if disabled (past debounce window).
     */
    std::array<uint16_t, 3> getLegPWMFromJoystick(float x, float y, uint64_t now_ms) {
        if (!_isEnabled) {
            if (now_ms - _lastEnabledChange < _debounceTimeout) {
                x = 0.0f;
                y = 0.0f;
            } else {
                return {0, 0, 0};
            }
        }
        std::tie(x, y) = adjustJoystickToAngleOffset(x, y);
        std::array<float, 3> legs = getLegAngles(x, y, _platformCurrentHeight);
        std::array<uint16_t, 3> leg_pwm = {0, 0, 0};
        for (size_t i = 0; i < legs.size(); ++i) {
            leg_pwm[i] = mapAngleToPWM(legs[i]) + _servoOffsetPWM[i];
        }
        return leg_pwm;
    }

    float getDeadband() const { return m_deadband; }
    void setDeadband(float db) { m_deadband = db; }

protected:
    static constexpr float kDefaultDeadband = 0.05f;
    float m_deadband = kDefaultDeadband;

private:
    float mapPWMToAngle(uint16_t pulseWidth) {
        return static_cast<float>(math::mapValue(
            pulseWidth, _servoTheoreticalMinPulse, _servoTheoreticalMaxPulse,
            0, _servoActuationRange));
    }

    uint16_t mapAngleToPWM(float angle) {
        return static_cast<uint16_t>(math::mapValue(
            static_cast<long>(std::round(angle)),
            0, _servoActuationRange,
            _servoTheoreticalMinPulse, _servoTheoreticalMaxPulse));
    }

    uint16_t _servoMinPulse[3] = {0, 0, 0};
    uint16_t _servoMaxPulse[3] = {0, 0, 0};
    float _servoOffsetAngle[3] = {0.0f, 0.0f, 0.0f};
    uint16_t _servoOffsetPWM[3] = {0, 0, 0};
    uint16_t _servoTheoreticalMinPulse = 0;
    uint16_t _servoTheoreticalMaxPulse = 0;
    uint16_t _servoActuationRange = 0;
    uint16_t _referenceMinPWM = 0;
    uint16_t _referenceMaxPWM = 0;
    uint64_t _lastEnabledChange = 0;
    uint16_t _debounceTimeout = 1000;
    bool _isEnabled = false;
    bool _hasBeenToggled = false;
    float _rotationRadianOffset = 0.0f;
    float _platformCurrentHeight = 0.0f;
    float _platformPreviousHeight = 0.0f;
};

} // namespace dome
} // namespace chopper
