#ifndef CHOPPER_SERVO_SERVOSTATE_H
#define CHOPPER_SERVO_SERVOSTATE_H

#include <Arduino.h>
#include "include/chopper/servo/Easing.h"
#include "SettingsSystem.h"

class ServoState {
public:
    ServoState() : 
        _currentPosition(0),
        _startPosition(0),
        _finishPosition(0),
        _startTime(0),
        _finishTime(0),
        _isDisabled(true)
    {
        setEasingMethod(nullptr);
    }

    void setEasingMethod(float (*easingMethod)(float completion)) {
        _easingMethod = easingMethod;
        if (_easingMethod == nullptr) {
            DEBUG_MAESTRO_PRINTF("Easing method is null, using default linear interpolation\n");
            _easingMethod = Easing::LinearInterpolation;
        }
    }

    void setRange(uint16_t startPulse, uint16_t finishPulse) {
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
    }

    void disable() {
        _isDisabled = true;
        _currentPosition = 0;
    }

    void enable(uint16_t position) {
        _isDisabled = false;
        _currentPosition = position;
    }

    void setTargets(uint16_t startPosition, uint16_t finishPosition, uint32_t duration)
    {
        uint32_t startTime = millis();
        setTargets(startPosition, finishPosition, startTime, startTime + duration);
    }

    void setTargets(uint16_t startPosition, uint16_t finishPosition, uint32_t startTime, uint32_t finishTime) {
        if (startTime > finishTime) {
            DEBUG_MAESTRO_PRINTF("Start time is greater than finish time: %d, %d\n", startTime, finishTime);
            return;
        }
        _startPosition = startPosition;
        _finishPosition = finishPosition;
        _startTime = startTime;
        _finishTime = finishTime;
    }

    uint16_t getNextPulse(uint32_t currentTime)
    {
        if (_isDisabled)
        {
            return 0;
        }
        uint16_t newPosition = _currentPosition;
        if (currentTime >= _startTime && currentTime <= _finishTime)
        {
            // Update the target position based on the time elapsed
            uint32_t elapsedDuration = currentTime - _startTime;
            uint32_t totalDuration = _finishTime - _startTime;
            float progress = static_cast<float>(elapsedDuration) / totalDuration;
            float easingFactor = _easingMethod(progress); 
            uint16_t distanceToMove = (_finishPosition - _currentPosition) * easingFactor;
            newPosition = _startPosition + distanceToMove;
            DEBUG_MAESTRO_PRINTF("progress: %d , easingFactor: %f , distanceToMove: %d , newPosition: %d\n", 
                progress, easingFactor, distanceToMove, newPosition);
            _currentPosition = newPosition;
        }
        else if (currentTime > _finishTime)
        {
            // If the time is past the finish time, set the position to the finish position
            newPosition = _finishPosition;
            _currentPosition = newPosition;
        }
        return newPosition;
    }

private:
    bool _isDisabled = false;
    uint16_t _startPulse;
    uint16_t _finishPulse;
    uint32_t _startTime;
    uint32_t _finishTime;
    uint16_t _startPosition;
    uint16_t _currentPosition; 
    uint16_t _finishPosition;
    float (*_easingMethod)(float completion) = nullptr;
};


#endif // CHOPPER_SERVO_SERVOSTATE_H