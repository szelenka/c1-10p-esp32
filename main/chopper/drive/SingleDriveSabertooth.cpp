#include "chopper/drive/SingleDriveSabertooth.h"


void SingleDriveSabertooth::SetRampingValue(int ramping) {
    SingleDrive::SetRampingValue(ramping);
}

void SingleDriveSabertooth::SetDeadband(float deadband)
{
    SingleDrive::SetDeadband(deadband);
}

void SingleDriveSabertooth::SetExpiration(uint64_t expirationTime)
{
    SingleDrive::SetExpiration(expirationTime);
};