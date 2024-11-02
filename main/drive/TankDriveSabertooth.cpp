#include "drive/TankDriveSabertooth.h"
#include "drive/TankDrive.h"
#include "motor/SabertoothDriver.h"

void TankDriveSabertooth::setup()
{
    setBaudRate(9600);
    setRamping(80);
    setTimeout(950);
}

void TankDriveSabertooth::stop()
{
    SabertoothDriver::stop();
    TankDrive::stop();
}

void TankDriveSabertooth::motor(float left, float right, float throttle)
{
    if (useThrottle()) 
    {
        left *= throttle;
        right *= throttle;
    }
    
    DEBUG_PRINTF(
        "[TankDriveSabertooth::motor] left: %2.6f, right: %2.6f, throttle: %2.6f\n",
        left, right, throttle
    );
    MOTOR_DEBUG_PRINT("ST1: ");
    MOTOR_DEBUG_PRINT((int)(left * 127));
    MOTOR_DEBUG_PRINT(" ST2: ");
    MOTOR_DEBUG_PRINTLN((int)(right * 127));
    SabertoothDriver::motor(1, left * 127);
    SabertoothDriver::motor(2, right * 127);
}
