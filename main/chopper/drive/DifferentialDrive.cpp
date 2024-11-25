// Copyright (c) FIRST and other WPILib contributors.
// Open Source Software; you can modify and/or share it under the terms of
// the WPILib BSD license file in the root directory of this project.

#include "chopper/drive/DifferentialDrive.h"

#include <algorithm>
#include <cmath>
#include <Bluepad32.h>

// #include <hal/FRCUsageReporting.h>
// #include <wpi/sendable/SendableBuilder.h>
// #include <wpi/sendable/SendableRegistry.h>

#include "MathUtil.h"
#include "chopper/motorController/MotorController.h"

// WPI_IGNORE_DEPRECATED

DifferentialDrive::DifferentialDrive(MotorController& leftMotor,
                                     MotorController& rightMotor)
    : DifferentialDrive{[&](double output) { leftMotor.Set(output); },
                        [&](double output) { rightMotor.Set(output); }} {
  // wpi::SendableRegistry::AddChild(this, &leftMotor);
  // wpi::SendableRegistry::AddChild(this, &rightMotor);
}

// WPI_UNIGNORE_DEPRECATED

DifferentialDrive::DifferentialDrive(std::function<void(double)> leftMotor,
                                     std::function<void(double)> rightMotor)
    : m_leftMotor{std::move(leftMotor)}, m_rightMotor{std::move(rightMotor)} {
  // static int instances = 0;
  // ++instances;
  // wpi::SendableRegistry::AddLW(this, "DifferentialDrive", instances);
}

void DifferentialDrive::ApplySpeedToMotors() {
  double left = ApplySpeedLimit(m_leftOutput, m_speedLimit);
  double right = ApplySpeedLimit(m_rightOutput, m_speedLimit);
  Console.printf("L: %1.3f R: %1.3f", left, right);
  m_leftMotor(left);
  m_rightMotor(right);
  Feed();
}

void DifferentialDrive::ArcadeDrive(double xSpeed, double zRotation,
                                    bool squareInputs) {
  // static bool reported = false;
  // if (!reported) {
  //   // HAL_Report(HALUsageReporting::kResourceType_RobotDrive,
  //   //            HALUsageReporting::kRobotDrive2_DifferentialArcade, 2);
  //   reported = true;
  // }

  xSpeed = ApplyDeadband(xSpeed, m_deadband);
  zRotation = ApplyDeadband(zRotation, m_deadband);

  auto [left, right] = ArcadeDriveIK(xSpeed, zRotation, squareInputs);

  m_leftOutput = left * m_maxOutput;
  m_rightOutput = right * m_maxOutput;

  ApplySpeedToMotors();
}

void DifferentialDrive::CurvatureDrive(double xSpeed, double zRotation,
                                        bool allowTurnInPlace) {
  // static bool reported = false;
  // if (!reported) {
  //   // HAL_Report(HALUsageReporting::kResourceType_RobotDrive,
  //   //            HALUsageReporting::kRobotDrive2_DifferentialCurvature, 2);
  //   reported = true;
  // }

  xSpeed = ApplyDeadband(xSpeed, m_deadband);
  zRotation = ApplyDeadband(zRotation, m_deadband);

  auto [left, right] = CurvatureDriveIK(xSpeed, zRotation, allowTurnInPlace);

  m_leftOutput = left * m_maxOutput;
  m_rightOutput = right * m_maxOutput;

  ApplySpeedToMotors();
}

void DifferentialDrive::ReelTwoDrive(double xSpeed, double zRotation,
                                      bool allowTurnInPlace) {
  // static bool reported = false;
  // if (!reported) {
  //   // HAL_Report(HALUsageReporting::kResourceType_RobotDrive,
  //   //            HALUsageReporting::kRobotDrive2_DifferentialCurvature, 2);
  //   reported = true;
  // }

  xSpeed = ApplyDeadband(xSpeed, m_deadband);
  zRotation = ApplyDeadband(zRotation, m_deadband);

  auto [left, right] = ReelTwoDriveIK(xSpeed, zRotation, allowTurnInPlace);

  m_leftOutput = left * m_maxOutput;
  m_rightOutput = right * m_maxOutput;

  ApplySpeedToMotors();
}

void DifferentialDrive::TankDrive(double leftSpeed, double rightSpeed,
                                  bool squareInputs) {
  // static bool reported = false;
  // if (!reported) {
  //   // HAL_Report(HALUsageReporting::kResourceType_RobotDrive,
  //   //            HALUsageReporting::kRobotDrive2_DifferentialTank, 2);
  //   reported = true;
  // }

  leftSpeed = ApplyDeadband(leftSpeed, m_deadband);
  rightSpeed = ApplyDeadband(rightSpeed, m_deadband);

  auto [left, right] = TankDriveIK(leftSpeed, rightSpeed, squareInputs);

  m_leftOutput = left * m_maxOutput;
  m_rightOutput = right * m_maxOutput;

  ApplySpeedToMotors();
}

DifferentialDrive::WheelSpeeds DifferentialDrive::ArcadeDriveIK(
    double xSpeed, double zRotation, bool squareInputs) {
  xSpeed = std::clamp(xSpeed, -1.0, 1.0);
  zRotation = std::clamp(zRotation, -1.0, 1.0);

  // Square the inputs (while preserving the sign) to increase fine control
  // while permitting full power.
  if (squareInputs) {
    xSpeed = std::copysign(xSpeed * xSpeed, xSpeed);
    zRotation = std::copysign(zRotation * zRotation, zRotation);
  }

  double leftSpeed = xSpeed - zRotation;
  double rightSpeed = xSpeed + zRotation;

  // Find the maximum possible value of (throttle + turn) along the vector that
  // the joystick is pointing, then desaturate the wheel speeds
  double greaterInput = (std::max)(std::abs(xSpeed), std::abs(zRotation));
  double lesserInput = (std::min)(std::abs(xSpeed), std::abs(zRotation));
  if (greaterInput == 0.0) {
    return {0.0, 0.0};
  }
  double saturatedInput = (greaterInput + lesserInput) / greaterInput;
  leftSpeed /= saturatedInput;
  rightSpeed /= saturatedInput;

  return {leftSpeed, rightSpeed};
}

DifferentialDrive::WheelSpeeds DifferentialDrive::CurvatureDriveIK(
    double xSpeed, double zRotation, bool allowTurnInPlace) {
  xSpeed = std::clamp(xSpeed, -1.0, 1.0);
  zRotation = std::clamp(zRotation, -1.0, 1.0);

  double leftSpeed = 0.0;
  double rightSpeed = 0.0;

  // TODO: when at very small values just outside deadzone, this can cause creap b/c it's not squared
  if (allowTurnInPlace) {
    leftSpeed = xSpeed - zRotation;
    rightSpeed = xSpeed + zRotation;
  } else {
    leftSpeed = xSpeed - std::abs(xSpeed) * zRotation;
    rightSpeed = xSpeed + std::abs(xSpeed) * zRotation;
  }

  // Desaturate wheel speeds
  double maxMagnitude = std::max(std::abs(leftSpeed), std::abs(rightSpeed));
  if (maxMagnitude > 1.0) {
    leftSpeed /= maxMagnitude;
    rightSpeed /= maxMagnitude;
  }

  return {leftSpeed, rightSpeed};
}

DifferentialDrive::WheelSpeeds DifferentialDrive::ReelTwoDriveIK(
    double xSpeed, double zRotation, bool squareInputs) {
  xSpeed = std::clamp(xSpeed, -1.0, 1.0);
  zRotation = std::clamp(zRotation, -1.0, 1.0);

  // exagerate the zRotation by 1.4
  // if (std::abs(zRotation) > m_deadband)
  //   zRotation = std::copysign(std::pow(std::abs(zRotation)-m_deadband, 1.4), zRotation);

  // Square the inputs (while preserving the sign) to increase fine control
  // while permitting full power.
  if (squareInputs) {
    xSpeed = std::copysign(xSpeed * xSpeed, xSpeed);
    zRotation = std::copysign(zRotation * zRotation, zRotation);
  }

  // Convert the cartesian coordinates to planar coordnates
  // ref: https://en.wikipedia.org/wiki/Atan2
  double ray = std::hypot(xSpeed, zRotation);
  double theta = std::atan2(zRotation, xSpeed);
  /*
  By rotating the joystick’s (x, y) coordinates by 45 degrees (i.e. pi/4 radians), we align the 
  joystick directions with the left and right motor requirements:

  - The rotated x-coordinate now represents a mix of forward/backward and left turning movement.
  - The rotated y-coordinate represents a mix of forward/backward and right turning movement.

  The 45-degree rotation simplifies the translation from joystick movement to differential drive
  control because it "tilts" the input space
  */
  theta += M_PI_4;
  double leftSpeed = ray * std::cos(theta);
  double rightSpeed = ray * std::sin(theta);

  /*
  After rotating by 45 degrees, the maximum values in each direction would be reduced to about 
  0.707 due to trigonometric scaling (cos(45°) = sin(45°) = 1/√2). To maintain the original input 
  range, we scale by √2, so that the motor commands can still reach their full range [-1.0, 1.0] 
  when the joystick is pushed to its extremes in the cardinal directions
  */
  leftSpeed *= std::sqrt(2);
  rightSpeed *= std::sqrt(2);

  /*
  Scaling the joystick inputs by √2 can over-compensates when the direction reaches near +/- 45-degree
  in each quadrant, so we need to desaturated the wheel speeds to get back in the [-1.0, 1.0] range
  for all directions
  */
  double maxMagnitude = std::max(std::abs(leftSpeed), std::abs(rightSpeed));
  if (maxMagnitude > 1.0) {
    leftSpeed /= maxMagnitude;
    rightSpeed /= maxMagnitude;
  }

  return {leftSpeed, rightSpeed};
}

DifferentialDrive::WheelSpeeds DifferentialDrive::TankDriveIK(
    double leftSpeed, double rightSpeed, bool squareInputs) {
  leftSpeed = std::clamp(leftSpeed, -1.0, 1.0);
  rightSpeed = std::clamp(rightSpeed, -1.0, 1.0);

  // Square the inputs (while preserving the sign) to increase fine control
  // while permitting full power.
  if (squareInputs) {
    leftSpeed = std::copysign(leftSpeed * leftSpeed, leftSpeed);
    rightSpeed = std::copysign(rightSpeed * rightSpeed, rightSpeed);
  }

  return {leftSpeed, rightSpeed};
}

void DifferentialDrive::StopMotor() {
  m_leftOutput = 0.0;
  m_rightOutput = 0.0;

  m_leftMotor(0.0);
  m_rightMotor(0.0);

  Feed();
}

std::string DifferentialDrive::GetDescription() const {
  return "DifferentialDrive";
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
