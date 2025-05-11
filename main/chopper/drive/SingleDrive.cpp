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
#include "SettingsSystem.h"

SingleDrive::SingleDrive(MotorController& motor)
    : SingleDrive{[&](float output) { motor.Set(output); }}
{
}

SingleDrive::SingleDrive(std::function<void(float)> motor)
    : m_motor{std::move(motor)}
{
}

void SingleDrive::ApplySpeedToMotor()
{
  float motor = ApplySpeedLimit(m_output, m_speedLimit);
  DEBUG_DOME_PRINTF("M: %1.3f ", motor);
  m_motor(motor);
  Feed();
}

void SingleDrive::Drive(float xSpeed, bool squareInputs)
{

  DEBUG_DOME_PRINTF("xS: %1.3f ", xSpeed);
  xSpeed = ApplyDeadband(xSpeed, m_deadband);

  float output = DriveIK(xSpeed, squareInputs);

  m_output = output * m_maxOutput;

  ApplySpeedToMotor();
}

float SingleDrive::DriveIK(float xSpeed, bool squareInputs)
{
  xSpeed = std::clamp(xSpeed, -1.0f, 1.0f);

  // Square the inputs (while preserving the sign) to increase fine control
  // while permitting full power.
  if (squareInputs) {
    xSpeed = std::copysign(xSpeed * xSpeed, xSpeed);
  }

  DEBUG_DOME_PRINTF("xS: %1.3f ", xSpeed);
  // TODO: apply calculation here
  float speed = xSpeed;

  return speed;
}

void SingleDrive::StopMotor()
{
  m_output = 0.0f;

  m_motor(0.0f);

  Feed();
}

std::string SingleDrive::GetDescription() const
{
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
