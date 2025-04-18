#ifndef CHOPPER_SERVO_DISPATCH_H
#define CHOPPER_SERVO_DISPATCH_H

#include <PololuMaestro.h>
// https://www.pololu.com/docs/0J40/5.e
// https://www.pololu.com/docs/0J40/5.f


class ServoDispatch : public MiniMaestro
{
public:
    ServoDispatch(Stream &stream, uint8_t resetPin = noResetPin, uint8_t deviceNumber = deviceNumberDefault, bool CRCEnabled = false) : 
        MiniMaestro(stream, resetPin, deviceNumber, CRCEnabled)
    {
        // Constructor for ServoDispatch
        // Initialize the Maestro with the given parameters
    };
    ~ServoDispatch() = default;

    /* 
        setTarget takes the channel number you want to control, and
        the target position in units of 1/4 microseconds. A typical
        RC hobby servo responds to pulses between 1 ms (4000) and 2
        ms (8000). 
    */

    void disable(uint8_t channel) {
        /*
        This command disables the specified channel.  A target value 
        of 0 tells the Maestro to stop sending pulses to the servo.
        */
        setTarget(channel, 0);
    }

    void disableAll() {
        uint16_t targets[24] = {0};
        setMultiTarget(24, 0, targets);
    }

};

#endif // CHOPPER_SERVO_DISPATCH_H