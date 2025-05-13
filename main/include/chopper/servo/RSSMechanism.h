#pragma once

#include "include/MathUtil.h"
#include "include/chopper/Timer.h"
#include "include/chopper/dome/RSSMachine.h"

class RSSMechanism : public RSSMachine
{
public:
    // Constructor
    RSSMechanism(float base_altitude, float end_effector_altitude, float bottom_link_length, float top_link_length, float min_height, float limit_normal_vector, bool bend_out)
        : RSSMachine(base_altitude, end_effector_altitude, bottom_link_length, top_link_length, min_height, limit_normal_vector, bend_out)
    {
        // Constructor implementation
        _platformCurrentHeight = (_platformMinHeight + _platformMaxHeight) / 2.0f;
        _platformPreviousHeight = _platformMinHeight;
    }
    
    ~RSSMechanism() = default;

    bool isEnabled()
    {
        return _isEnabled;
    }

    void setEnabled(bool enabled)
    {
        uint64_t currentTime = Timer::GetFPGATimestamp();
        if (currentTime - _debounceTimeout < _lastEnabledChange)
        {
            DEBUG_RSS_MACHINE_PRINTF("RSSMachine: setEnabled: %d - debounce\n", enabled);
            return;
        }
        DEBUG_RSS_MACHINE_PRINTF("RSSMachine: setEnabled: %d\n", enabled);
        _isEnabled = enabled;
        _lastEnabledChange = currentTime;
        if (_isEnabled)
        {
            // go to midpoint
            DEBUG_RSS_MACHINE_PRINTF("RSSMachine: setEnabled: %d - go to mid\n", _isEnabled);
            // real-world testing showed that slighly lower than max looked best
            setHeight(_platformMaxHeight - 5.0f);
        }
        else
        {
            DEBUG_RSS_MACHINE_PRINTF("RSSMachine: setEnabled: %d - go to low\n", _isEnabled);
            setHeight(_platformMinHeight);
        }
    }

    void setRotationAngleOffset(float angle)
    {
        // compute radians to make other calculations easier
        _rotationRadianOffset = constrain(angle, 0.0f, 160.0f) * (M_PI / 180.0f);;
    }

    void setActuationRange(uint16_t actuationRange)
    {
        switch(actuationRange)
        {
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

    void setLegMinPulse(uint16_t minA, uint16_t minB, uint16_t minC)
    {
        _servoMinPulse[0] = minA;
        _servoMinPulse[1] = minB;
        _servoMinPulse[2] = minC;
        _referenceMinPWM = static_cast<uint16_t>(round(
            static_cast<float>(minA) + static_cast<float>(minB) + static_cast<float>(minC)) / 3.0f
        );
        calculateLegOffsets();
    }

    void setLegMaxPulse(uint16_t maxA, uint16_t maxB, uint16_t maxC)
    {
        _servoMaxPulse[0] = maxA;
        _servoMaxPulse[1] = maxB;
        _servoMaxPulse[2] = maxC;
        _referenceMaxPWM = static_cast<uint16_t>(round(
            static_cast<float>(maxA) + static_cast<float>(maxB) + static_cast<float>(maxC)) / 3.0f
        );
        calculateLegOffsets();
    }

    void incrementHeight(float increment)
    {
        _platformPreviousHeight = _platformCurrentHeight;
        _platformCurrentHeight = std::min(_platformMaxHeight, std::max(_platformMinHeight, _platformCurrentHeight + increment));
    }

    void decrementHeight(float decrement)
    {
        _platformPreviousHeight = _platformCurrentHeight;
        _platformCurrentHeight = std::min(_platformMaxHeight, std::max(_platformMinHeight, _platformCurrentHeight - decrement));
    }

    void setHeight(float height)
    {
        _platformPreviousHeight = _platformCurrentHeight;
        _platformCurrentHeight = std::min(_platformMaxHeight, std::max(_platformMinHeight, height));
    }

    void calculateLegOffsets()
    {
        uint16_t platformMinHeightPWM = mapAngleToPWM(_platformMinHeightAngle);
        uint16_t platformMaxHeightPWM = mapAngleToPWM(_platformMaxHeightAngle);
        uint16_t offset = 0;

        for (size_t i = 0; i < 3; ++i)
        {
            if (_servoMinPulse[i] != 0 && _servoMaxPulse[i] == 0)
            {
                offset = std::max(_referenceMinPWM, _referenceMaxPWM) - std::max(platformMinHeightPWM, platformMaxHeightPWM);
                _servoOffsetPWM[i] = offset + (_servoMinPulse[i] - _referenceMinPWM);
            }
            else if (_servoMinPulse[i] == 0 || _servoMaxPulse[i] != 0)
            {
                offset = std::min(_referenceMinPWM, _referenceMaxPWM) - std::min(platformMinHeightPWM, platformMaxHeightPWM);
                _servoOffsetPWM[i] = offset + (_servoMaxPulse[i] - _referenceMaxPWM);
            }
            else
            {
                offset = (
                    std::max(_referenceMinPWM, _referenceMaxPWM) - std::max(platformMinHeightPWM, platformMaxHeightPWM)
                ) + (
                    std::min(_referenceMinPWM, _referenceMaxPWM) - std::min(platformMinHeightPWM, platformMaxHeightPWM)
                ) / 2;
                _servoOffsetPWM[i] = offset + (
                    (_servoMinPulse[i] - _referenceMinPWM) + (_servoMaxPulse[i] - _referenceMaxPWM)
                ) / 2;
            }
            _servoOffsetAngle[i] = mapPWMToAngle(_servoOffsetPWM[i]);
            DEBUG_RSS_MACHINE_PRINTF(
                "RSS[Leg]: %zu: minPulse: %u maxPulse: %u offsetPulse: %u offsetAngle: %4.2f, relativeAngleOffset: %4.2f\n", 
                i, _servoMinPulse[i], _servoMaxPulse[i], _servoOffsetPWM[i], _servoOffsetAngle[i], _servoOffsetAngle[i] - mapPWMToAngle(offset)
            );
        }
    }
    
    std::tuple<float, float> adjustJoystickToAngleOffset(float& x, float& y)
    {
        // Adjust the joystick input based on the rotation angle offset
        // This is a simple rotation transformation
        // around the origin (0, 0) by the specified angle
        // This allows joystick input for straight up, to align with straight up on the triangle of the RSS Mechanism
        x = round(ApplyDeadband(x, m_deadband) * 10.0f) / 10.0f;
        y = round(ApplyDeadband(y, m_deadband) * 10.0f) / 10.0f;
        float rotatedX = x * cos(_rotationRadianOffset) - y * sin(_rotationRadianOffset);
        float rotatedY = x * sin(_rotationRadianOffset) + y * cos(_rotationRadianOffset);
        return std::make_tuple(rotatedX, rotatedY);
    }

    std::array<float, 3> getLegAnglesFromJoystick(float x, float y)
    {
        if (!_isEnabled)
        {
            uint64_t currentTime = Timer::GetFPGATimestamp();
            if (currentTime - _debounceTimeout < _lastEnabledChange)
            {
                x = 0.0f;
                y = 0.0f;
            }
            else
            {
                return {0.0f, 0.0f, 0.0f};
            }
            x = 0.0f;
            y = 0.0f;
        }
        std::tie(x, y) = adjustJoystickToAngleOffset(x, y);
        std::array<float, 3> legs = getLegAngles(x, y, _platformCurrentHeight);
        for (size_t i = 0; i < legs.size(); ++i)
        {
            legs[i] = round(legs[i] + _servoOffsetAngle[i]);
        }
        DEBUG_RSS_MACHINE_PRINTF("A: %4.2f, B: %4.2f, C: %4.2f\n", legs[0], legs[1], legs[2]);
        return legs;
    }

    std::array<uint16_t, 3> getLegPWMFromJoystick(float x, float y)
    {
        if (!_isEnabled)
        {
            uint64_t currentTime = Timer::GetFPGATimestamp();
            if (currentTime - _debounceTimeout < _lastEnabledChange)
            {
                x = 0.0f;
                y = 0.0f;
            }
            else
            {
                return {0, 0, 0};
            }
        }
        std::tie(x, y) = adjustJoystickToAngleOffset(x, y);
        std::array<float, 3> legs = getLegAngles(x, y, _platformCurrentHeight);
        std::array<uint16_t, 3> leg_pwm = {0, 0, 0};
        for (size_t i = 0; i < legs.size(); ++i)
        {
            leg_pwm[i] = mapAngleToPWM(legs[i]) + _servoOffsetPWM[i];
        }
        DEBUG_RSS_MACHINE_PRINTF("A: %4u, B: %4u, C: %4u\n", leg_pwm[0], leg_pwm[1], leg_pwm[2]);
        return leg_pwm;
    }

protected:
  /// Default input deadband.
  static constexpr float kDefaultDeadband = 0.05f; // 0.125f ?
  /// Input deadband.
  float m_deadband = kDefaultDeadband;

private:
    float mapPWMToAngle(uint16_t pulseWidth)
    {
        // Convert PWM signal to angle
        return static_cast<float>(map(
            pulseWidth, 
            _servoTheoreticalMinPulse, 
            _servoTheoreticalMaxPulse, 
            0, 
            _servoActuationRange
        ));
    }

    uint16_t mapAngleToPWM(float angle)
    {
        // Convert angle to PWM signal
        // Add the offset to the angle (accommodate for real-world servo installation)
        return map(
            static_cast<uint16_t>(round(angle)),
            0, 
            _servoActuationRange, 
            _servoTheoreticalMinPulse, 
            _servoTheoreticalMaxPulse
        );
    }

    // float normalizeVectorFloatInput(float rawValue, float minValue, float maxValue) 
    // {
    //     return std::clamp(
    //         ((rawValue - minValue) * (_limit_normal_vector - (-_limit_normal_vector))) / (maxValue - minValue) + (-_limit_normal_vector),
    //         -_limit_normal_vector, 
    //         _limit_normal_vector);
    // }

    uint16_t _servoMinPulse[3] = {0, 0, 0};
    uint16_t _servoMaxPulse[3] = {0, 0, 0};
    float _servoOffsetAngle[3] = {0.0f, 0.0f, 0.0f};
    uint16_t _servoOffsetPWM[3] = {0, 0, 0};
    uint16_t _servoTheoreticalMinPulse = 0;
    uint16_t _servoTheoreticalMaxPulse = 0;
    uint16_t _servoActuationRange = 0;
    uint16_t _referenceMinPWM = 0;
    uint16_t _referenceMaxPWM = 0;
    uint64_t _lastEnabledChange = Timer::GetFPGATimestamp();
    uint16_t _debounceTimeout = 1000;
    bool _isEnabled = false;
    float _rotationRadianOffset = 0.0f;
    float _platformCurrentHeight = 0.0f;
    float _platformPreviousHeight = 0.0f;
};
