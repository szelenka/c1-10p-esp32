#ifndef TankDriveSabertooh_h
#define TankDriveSabertooh_h
#include <Arduino.h>
#include "SabertoothDriver.h"
#include "TankDrive.h"

class TankDriveSabertooth : public TankDrive, public SabertoothDriver
{
public:
    TankDriveSabertooth(int id, Stream& serial) :
        TankDrive(),
        SabertoothDriver(id, serial)
    {
    }

    virtual void setup() override;

    virtual void stop() override;

protected:
    virtual void motor(float left, float right, float throttle) override;
};
#endif
