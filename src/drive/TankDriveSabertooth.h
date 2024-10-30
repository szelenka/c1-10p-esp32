#ifndef TankDriveSabertooh_h
#define TankDriveSabertooh_h

#include "settings/system.h"
#include "settings/user.h"
#include "drive/TankDrive.h"
#include <Sabertooth.h>

class TankDriveSabertooth : public TankDrive, public Sabertooth {

    virtual void setup() override
    {
        setBaudRate(SABERTOOTH_SERIAL_BAUD_RATE);
        setRamping(C110P_RAMPING_PERIOD);
        setTimeout(C110P_MOTOR_TIMEOUT_MS);
    }
    virtual void stop() override
    {
        Sabertooth::stop();
        TankDrive::stop();
    }
protected:
    virtual void motor(float left, float right, float throttle) override
    {
        left *= throttle;
        right *= throttle;
        MOTOR_DEBUG_PRINT("ST1: ");
        MOTOR_DEBUG_PRINT((int)(left * 127));
        MOTOR_DEBUG_PRINT(" ST2: ");
        MOTOR_DEBUG_PRINTLN((int)(right * 127));
        Sabertooth::motor(1, left * 127);
        Sabertooth::motor(2, right * 127);
    }
};
#endif
