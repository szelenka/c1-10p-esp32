#pragma once

#include "chopper/drive/DifferentialDrive.h"
#include "chopper/motorController/SabertoothController.h"
#include <Sabertooth.h>
#include <Bluepad32.h>

class SabertoothDrive {
public:
  SabertoothDrive(byte address, Stream& stream, int leftMotorId = 1, int rightMotorId = 2) :
    m_sabertoothDriver(address, stream),
    m_leftMotor(m_sabertoothDriver, leftMotorId),
    m_rightMotor(m_sabertoothDriver, rightMotorId)
  {
    Console.println("SabertoothDrive [init]");
    m_sabertoothDriver.setBaudRate(9600);
  }

  SabertoothController& GetLeftMotor() {
    return m_leftMotor;
  }
  SabertoothController& GetRightMotor() {
    return m_rightMotor;
  }

private:
  Sabertooth m_sabertoothDriver;
  SabertoothController m_leftMotor;
  SabertoothController m_rightMotor;
};

class DifferentialDriveSabertooth : public DifferentialDrive
{ 
 public:
  // static DifferentialDriveSabertooth create(int sabertoothId, Stream& serial, int leftMotorId = 1, int rightMotorId = 2)
  // {
  //   m_sabertoothDriver = Sabertooth((byte)sabertoothId, serial);
  //   m_sabertoothDriver.setBaudRate(9600);
  //   m_leftMotor = SabertoothController(m_sabertoothDriver, leftMotorId);
  //   m_rightMotor = SabertoothController(m_sabertoothDriver, rightMotorId);
  //   return DifferentialDriveSabertooth(m_leftMotor, m_rightMotor);
  // }
  void SetRamping(int ramping);
  void SetDeadband(double deadband);
  void SetExpiration(uint64_t expirationTime);

 protected:
  // static Sabertooth m_sabertoothDriver;
  // static SabertoothController m_leftMotor;
  // static SabertoothController m_rightMotor;
  // DifferentialDriveSabertooth(MotorController& leftMotor, MotorController& rightMotor) : 
  //   DifferentialDrive(leftMotor, rightMotor) {}
};
