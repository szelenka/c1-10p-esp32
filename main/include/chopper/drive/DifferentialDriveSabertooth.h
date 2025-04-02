#pragma once

#include "chopper/drive/DifferentialDrive.h"
#include "chopper/motorController/SabertoothController.h"
#include <Sabertooth.h>
#include <Bluepad32.h>
#include "SettingsSystem.h"

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
  void SetRampingValue(int ramping);
  void SetDeadband(double deadband);
  void SetExpiration(uint64_t expirationTime);

 protected:
  // static Sabertooth m_sabertoothDriver;
  // static SabertoothController m_leftMotor;
  // static SabertoothController m_rightMotor;
  // DifferentialDriveSabertooth(MotorController& leftMotor, MotorController& rightMotor) : 
  //   DifferentialDrive(leftMotor, rightMotor) {}
};
