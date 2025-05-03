#ifndef CHOPPER_SERVO_DISPATCH_H
#define CHOPPER_SERVO_DISPATCH_H

#include <PololuMaestro.h>
// https://www.pololu.com/docs/0J40/5.e
// https://www.pololu.com/docs/0J40/5.f
#include "include/chopper/servo/ServoState.h"
#include "include/settings/ServoPWM.h"

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
                    _servoStates[i].setRange(MAESTRO_UTILITY_ARM_MIN, MAESTRO_UTILITY_ARM_MAX, MAESTRO_UTILITY_ARM_NEUTRAL);
                    _servoStates[i].setEasingMethod(Easing::SineEaseInOut);
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

    void setTargetMicrosecond(uint8_t channel, uint16_t target)
    {
        /* 
            setTarget takes the channel number you want to control, and
            the target position in units of 1/4 microseconds. A typical
            RC hobby servo responds to pulses between 1 ms (4000) and 2
            ms (8000). 
        */
        MiniMaestro::setTarget(channel, target * 4);
    }

    void animate()
    {
        uint32_t currentTime = millis();
        for (uint8_t i = 0; i < _channels; ++i)
        {
            _channelTargets[i] = _servoStates[i].getNextPulse(currentTime);
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

    void setTimedMovement(uint8_t channel, uint16_t startPosition, uint16_t finishPosition, uint16_t duration)
    {
        uint16_t currentPosition = MiniMaestro::getPosition(channel);
        if (currentPosition == finishPosition)
        {
            _servoStates[channel].disable();
        }
        else
        {
            _servoStates[channel].setTargets(
                startPosition, 
                finishPosition, 
                duration);
            _servoStates[channel].enable(currentPosition);
        }
    }


private:
    uint8_t _channels;
    std::vector<uint16_t> _channelTargets;
    std::vector<ServoState> _servoStates;
};

#endif // CHOPPER_SERVO_DISPATCH_H