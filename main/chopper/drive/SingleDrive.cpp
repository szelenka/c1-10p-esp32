// Copyright (c) FIRST and other WPILib contributors.
// Open Source Software; you can modify and/or share it under the terms of
// the WPILib BSD license file in the root directory of this project.

#include "chopper/drive/SingleDrive.h"

#include <algorithm>
#include <cmath>
#include <Bluepad32.h>

// #include <hal/FRCUsageReporting.h>
// #include <wpi/sendable/SendableBuilder.h>
// #include <wpi/sendable/SendableRegistry.h>

#include "MathUtil.h"
#include "chopper/motorController/MotorController.h"

SingleDrive::SingleDrive(MotorController& motor)
    : SingleDrive{[&](double output) { motor.Set(output); }} {
}

SingleDrive::SingleDrive(std::function<void(double)> motor)
    : m_motor{std::move(motor)} {
}

void SingleDrive::ApplySpeedToMotor() {
  double motor = ApplySpeedLimit(m_output, m_speedLimit);
  m_motor(motor);
  Feed();
}

void SingleDrive::Drive(double xSpeed, bool squareInputs) {

  xSpeed = ApplyDeadband(xSpeed, m_deadband);

  double output = DriveIK(xSpeed, squareInputs);

  m_output = output * m_maxOutput;

  ApplySpeedToMotor();
}

double SingleDrive::DriveIK(double xSpeed, bool squareInputs) {
  xSpeed = std::clamp(xSpeed, -1.0, 1.0);

  // Square the inputs (while preserving the sign) to increase fine control
  // while permitting full power.
  if (squareInputs) {
    xSpeed = std::copysign(xSpeed * xSpeed, xSpeed);
  }

  // TODO: apply calculation here
  double speed = xSpeed;

  return speed;
}

void SingleDrive::StopMotor() {
  m_output = 0.0;

  m_motor(0.0);

  Feed();
}

std::string SingleDrive::GetDescription() const {
  return "SingleDrive";
}

// void DifferentialDrive::InitSendable(wpi::SendableBuilder& builder) {
//   builder.SetSmartDashboardType("DifferentialDrive");
//   builder.SetActuator(true);
//   builder.SetSafeState([=, this] { StopMotor(); });
//   builder.AddDoubleProperty(
//       "Left Motor Speed", [&] { return m_leftOutput; }, m_leftMotor);
//   builder.AddDoubleProperty(
//       "Right Motor Speed", [&] { return m_rightOutput; }, m_rightMotor);
// }
