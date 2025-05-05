#ifndef CHOPPER_SERVO_DISPATCH_H
#define CHOPPER_SERVO_DISPATCH_H

#include <PololuMaestro.h>
// https://www.pololu.com/docs/0J40/5.e
// https://www.pololu.com/docs/0J40/5.f
#include "include/chopper/servo/ServoState.h"
#include "include/settings/ServoPWM.h"
#include "include/Timer.h"

class ServoDispatch : public MiniMaestro
{
public:
    ServoDispatch(Stream &stream, uint8_t resetPin = noResetPin, uint8_t deviceNumber = deviceNumberDefault, bool CRCEnabled = false, uint8_t channels = 24) : 
        MiniMaestro(stream, resetPin, deviceNumber, CRCEnabled), 
        _channels(channels), 
        _servoStates(channels, ServoState()),
        _channelTargets(channels, 0)
    {
        for (uint8_t i = 0; i < _channels; ++i)
        {
            switch(i)
            {
                case MAESTRO_UTILITY_ARM:
                    MiniMaestro::setSpeed(i, MAESTRO_UTILITY_ARM_SPEED);
                    MiniMaestro::setAcceleration(i, MAESTRO_UTILITY_ARM_ACCEL);
                    _servoStates[i].setRange(MAESTRO_UTILITY_ARM_MIN, MAESTRO_UTILITY_ARM_MAX, MAESTRO_UTILITY_ARM_NEUTRAL);
                    _servoStates[i].setEasingMethod(Easing::CubicEaseInOut);
                    _servoStates[i].setPosition(MAESTRO_UTILITY_ARM_NEUTRAL);
                    break;
                // case MAESTRO_BODY_NECK_A:
                //     _servoStates[i].setRange(MAESTRO_BODY_NECK_A_MIN, MAESTRO_BODY_NECK_A_MAX);
                //     _servoStates[i].setEasingMethod(Easing::LinearInterpolation);
                //     break;
                // case MAESTRO_BODY_NECK_B:
                //     _servoStates[i].setRange(MAESTRO_BODY_NECK_B_MIN, MAESTRO_BODY_NECK_B_MAX);
                //     _servoStates[i].setEasingMethod(Easing::LinearInterpolation);
                //     break;
                // case MAESTRO_BODY_NECK_C:
                //     _servoStates[i].setRange(MAESTRO_BODY_NECK_C_MIN, MAESTRO_BODY_NECK_C_MAX);
                //     _servoStates[i].setEasingMethod(Easing::LinearInterpolation);
                //     break;
                default:
                    break;
            }
        }
    };
    ~ServoDispatch() = default;

    void animate()
    {
        uint32_t currentTime = Timer::GetFPGATimestamp();
        for (uint8_t i = 0; i < _channels; ++i)
        {
            // setMultiTarget command requires the target to be in 1/4 microsecond units, so we multiply by 4
            _channelTargets[i] = _servoStates[i].getNextPulse(currentTime) * 4;
        }
        MiniMaestro::setMultiTarget(_channels, 0, _channelTargets.data());
    }

    void disable(uint8_t channel)
    {
        /*
            This command disables the specified channel.  A target value 
            of 0 tells the Maestro to stop sending pulses to the servo.
        */
        _channelTargets[channel] = 0;
        _servoStates[channel].disable();
        MiniMaestro::setTarget(channel, 0);
    }

    void disableAll()
    {
        std::fill(_channelTargets.begin(), _channelTargets.end(), 0);
        for (uint8_t i = 0; i < _channels; ++i)
        {
            _servoStates[i].disable();
        }
        animate();
    }

    void setPotision(uint8_t channel, uint16_t position)
    {
        _servoStates[channel].setPosition(position);
    }

    void setTimedMovement(uint8_t channel, uint16_t startPosition, uint16_t finishPosition, uint32_t startTime, uint32_t duration)
    {
        _servoStates[channel].setTargets(startPosition, finishPosition, startTime, startTime + duration);
        if (_servoStates[channel].isFinishedMoving())
        {
            // Servo has reached finish position, disable it to prevent PWM searching/jitter
            _servoStates[channel].disable();
        }
        else
        {
            _servoStates[channel].enable();
        }
    }


private:
    uint8_t _channels;
    std::vector<uint16_t> _channelTargets;
    std::vector<ServoState> _servoStates;
};

#endif // CHOPPER_SERVO_DISPATCH_H