#ifndef TankDriveSabertooh_h
#define TankDriveSabertooh_h

#include "<Sabertooth.h>"
#include "TankDrive.h"

class TankDriveSabertooth : public TankDrive, public Sabertooth
{
public:
    TankDriveSabertooth(int id, HardwareSerial& serial) :
        TankDrive(),
        Sabertooth(id, serial)
    {
    }

    virtual void setup();

    virtual void stop();

protected:
    virtual void motor(float left, float right, float throttle);
};
#endif
