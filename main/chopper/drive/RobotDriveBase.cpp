// Copyright (c) FIRST and other WPILib contributors.
// Open Source Software; you can modify and/or share it under the terms of
// the WPILib BSD license file in the root directory of this project.

#include "chopper/drive/RobotDriveBase.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

// #include <hal/FRCUsageReporting.h>

#include "chopper/motorController/MotorController.h"

RobotDriveBase::RobotDriveBase() {
  SetSafetyEnabled(true);
}

void RobotDriveBase::SetRampingValue(int ramping) {
  /*
  This adjusts or disables the ramping feature found on the Sabertooth 2x35. This adjustment
  applies to all modes, even R/C and analog mode

  Values between 1 and 10 are Fast Ramp
  Values between 11 and 20 are Slow Ramp

  Values between 21 and 80 are Intermediate Ramp.

  Fast Ramping is a ramp time of 256/(~1000xCommand value). Ramp time is the delay between
  full forward and full reverse speed.
  1: 1/4 second ramp (default)
  2: 1/8 second ramp
  3: 1/12 second ramp

  Slow and Intermediate Ramping are a ramp time of 256/[15.25x(Command value  10)]
  */
  m_rampingValue = std::clamp(ramping, 0, 80);
}

void RobotDriveBase::SetDeadband(double deadband) {
  m_deadband = deadband;
}

void RobotDriveBase::SetMaxOutput(double maxOutput) {
  m_maxOutput = maxOutput;
}

void RobotDriveBase::SetSpeedLimit(double limit) {
  m_speedLimit = std::clamp(limit, 0.0, 1.0);
}

void RobotDriveBase::FeedWatchdog() {
  Feed();
}

// void RobotDriveBase::Desaturate(std::span<double> wheelSpeeds) {
//   double maxMagnitude = std::abs(wheelSpeeds[0]);
//   for (size_t i = 1; i < wheelSpeeds.size(); i++) {
//     double temp = std::abs(wheelSpeeds[i]);
//     if (maxMagnitude < temp) {
//       maxMagnitude = temp;
//     }
//   }
//   if (maxMagnitude > 1.0) {
//     for (size_t i = 0; i < wheelSpeeds.size(); i++) {
//       wheelSpeeds[i] = wheelSpeeds[i] / maxMagnitude;
//     }
//   }
// }
