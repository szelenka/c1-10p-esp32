#include "chopper/drive/DifferentialDriveSabertooth.h"

// void DifferentialDriveSabertooth::setup()
// {
//     m_saberToothDriver.setBaudRate(9600);
//     SetRamping(80);
//     SetExpiration(950);
// };

void DifferentialDriveSabertooth::SetRampingValue(int ramping)
{
    // m_sabertoothDriver.setRamping((byte)(ramping));
    DifferentialDrive::SetRampingValue(ramping);
}

void DifferentialDriveSabertooth::SetDeadband(float deadband)
{
    // m_sabertoothDriver.setDeadband((byte)(deadband*127));
    DifferentialDrive::SetDeadband(deadband);
}

void DifferentialDriveSabertooth::SetExpiration(uint64_t expirationTime)
{
    // m_sabertoothDriver.setTimeout(expirationTime);
    DifferentialDrive::SetExpiration(expirationTime);
};