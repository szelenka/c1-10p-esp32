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

void RobotDriveBase::SetRamping(uint64_t ramping) {
  m_ramping = ramping;
}

void RobotDriveBase::SetDeadband(double deadband) {
  m_deadband = deadband;
}

void RobotDriveBase::SetMaxInput(double maxInput) {
  m_maxInput = maxInput;
}

void RobotDriveBase::SetMinInput(double minInput) {
  m_minInput = minInput;
}

void RobotDriveBase::SetMaxOutput(double maxOutput) {
  m_maxOutput = maxOutput;
}

void RobotDriveBase::SetMinOutput(double minOutput) {
  m_minOutput = minOutput;
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
