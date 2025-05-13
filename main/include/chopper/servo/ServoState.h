#ifndef CHOPPER_SERVO_SERVOSTATE_H
#define CHOPPER_SERVO_SERVOSTATE_H

#include <Arduino.h>
#include "include/chopper/servo/Easing.h"
#include "include/chopper/filter/SlewRateLimiter.h"
#include "SettingsSystem.h"
#include "include/chopper/Timer.h"

class ServoState
{
public:
    ServoState() : 
        _currentPosition(0),
        _startPosition(0),
        _finishPosition(0),
        _startTime(Timer::GetFPGATimestamp()),
        _finishTime(Timer::GetFPGATimestamp()),
        _totalDuration(_finishTime - _startTime),
        _rateLimit(2000),
        _isDisabled(true)
    {
        setEasingMethod(nullptr);
    }

    void setEasingMethod(float (*easingMethod)(float completion))
    {
        _easingMethod = easingMethod;
        if (_easingMethod == nullptr) {
            DEBUG_MAESTRO_PRINTF("Easing method is null, using default linear interpolation\n");
            _easingMethod = Easing::LinearInterpolation;
        }
    }

    void setRange(uint16_t startPulse, uint16_t finishPulse, uint16_t neutralPulse)
    {
        if (startPulse == 0 || finishPulse == 0) {
            DEBUG_MAESTRO_PRINTF("Start or finish pulse is zero: %d, %d\n", startPulse, finishPulse);
            return;
        }
        if (startPulse > finishPulse) {
            DEBUG_MAESTRO_PRINTF("Start pulse is greater than finish pulse: %d, %d\n", startPulse, finishPulse);
            return;
        }
        _startPulse = startPulse;
        _finishPulse = finishPulse;
        _neutralPulse = constrain(neutralPulse, startPulse, finishPulse);
    }

    void setEnable(bool isEnabled)
    {
        _isDisabled = !isEnabled;
    }

    void setManual(bool isManual)
    {
        _isManual = isManual;
        if (isManual) {
            _startPosition = _currentPosition;
            _finishPosition = _currentPosition;
            _startTime = Timer::GetFPGATimestamp();
            _finishTime = Timer::GetFPGATimestamp();
            _totalDuration = 0;
        }
    }

    bool isFinishedMoving()
    {
        return (Timer::GetFPGATimestamp() > _finishTime && _currentPosition == _finishPosition);
    }

    void setPosition(uint16_t position)
    {
        if (position == 0) {
            // this effectively disables the servo, manual controls may want to do this
            _currentPosition = 0;
            return;
        }
        _currentPosition = constrain(position, _startPulse, _finishPulse);
    }

    void setAngle(float angle, uint16_t actuationRange, uint16_t minPulse, uint16_t maxPulse)
    {
        if (isnan(angle) || isinf(angle) || angle < 0.0f || angle > static_cast<float>(actuationRange)) {
            DEBUG_MAESTRO_PRINTF("Angle out of range: %.2f\n", angle);
            return;
        }
        // Convert angle to pulse width
        uint16_t pulseWidth = map(
            static_cast<uint16_t>(round(angle)), 
            0, 
            actuationRange, 
            minPulse,       // theoretical min pulse width
            maxPulse);      // theoretical max pulse width
        DEBUG_MAESTRO_PRINTF("Angle: %.2f, Pulse Width: %d\n", angle, pulseWidth);
        setPosition(pulseWidth);
    }

    void setTargets(uint16_t startPosition, uint16_t finishPosition, uint64_t startTime, uint64_t finishTime)
    {
        if (startPosition == _startPosition && finishPosition == _finishPosition)
        {
            // duplicate call to something already in motion, no need to reassign
            return;
        }
        if (startPosition == 0 || finishPosition == 0)
        {
            DEBUG_MAESTRO_PRINTF("Start or finish position is zero: %d, %d\n", startPosition, finishPosition);
            return;
        }
        if (startTime > finishTime) {
            DEBUG_MAESTRO_PRINTF("Start time is greater than finish time: %d, %d\n", startTime, finishTime);
            return;
        }
        _startPosition = constrain(startPosition, _startPulse, _finishPulse);
        _finishPosition = constrain(finishPosition, _startPulse, _finishPulse);
        _startTime = startTime;
        _finishTime = finishTime;
        if (_startPosition != _currentPosition)
        {
            // if we are already in motion, we need to adjust the start and finish times
            // based on the current position and the new start and finish positions
            float existingProgress = fabs(static_cast<float>(_currentPosition - _startPosition) / (_finishPosition - _startPosition));
            _startTime = startTime - static_cast<uint64_t>(existingProgress * (_finishTime - startTime));
            _finishTime = finishTime - static_cast<uint64_t>(existingProgress * (_finishTime - startTime));
        }
        _totalDuration = _finishTime - _startTime;
        DEBUG_MAESTRO_PRINTF("start: %d, finish: %d, current: %d, startTime: %d, startTime: %d, finishTime: %d, finishTime: %d, totalDuration: %d\n", 
            _startPosition,
            _finishPosition,
            _currentPosition,
            _startTime,
            startTime,
            _finishTime,
            finishTime,
            _totalDuration);
    }

    uint16_t getNextPulse(uint64_t currentTime)
    {
        if (_isDisabled)
        {
            return 0;
        }
        else if (_isManual)
        {
            return _currentPosition;
        }
        uint16_t newPosition = _currentPosition;
        float easingFactor = 1.0f;
        int16_t distanceToMove = 0;
        uint64_t elapsedDuration = currentTime - _startTime;
        float progress = static_cast<float>(elapsedDuration) / _totalDuration;
        if (progress < 1.0f)
        {
            // Update the target position based on the time elapsed
            easingFactor = _easingMethod(progress); 
            distanceToMove = (_finishPosition - _startPosition) * easingFactor;

            newPosition = _rateLimit.Calculate(constrain(_startPosition + distanceToMove, _startPulse, _finishPulse));
        }
        else if (currentTime > _finishTime)
        {
            // If the time is past the finish time, set the position to the finish position
            newPosition = _rateLimit.Calculate(_finishPosition);
        }
        DEBUG_MAESTRO_PRINTF("progress: %.3f , easingFactor: %.3f , distanceToMove: %d , newPosition: %d\n", 
            progress,
            easingFactor,
            distanceToMove,
            newPosition);
        _currentPosition = newPosition;
        return newPosition;
    }

private:
    bool _isDisabled = false;
    bool _isManual = false;
    uint16_t _startPulse;
    uint16_t _finishPulse;
    uint16_t _neutralPulse;
    uint64_t _startTime;
    uint64_t _finishTime;
    uint64_t _totalDuration;
    uint16_t _startPosition;
    uint16_t _currentPosition; 
    uint16_t _finishPosition;
    SlewRateLimiter _rateLimit;
    float (*_easingMethod)(float completion) = nullptr;
};


#endif // CHOPPER_SERVO_SERVOSTATE_H