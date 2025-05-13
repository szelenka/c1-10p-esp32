// Copyright (c) FIRST and other WPILib contributors.
// Open Source Software; you can modify and/or share it under the terms of
// the WPILib BSD license file in the root directory of this project.

#pragma once

#include <memory>
// #include <span>
#include <string>

#include "chopper/MotorSafety.h"

/**
 * Common base class for drive platforms.
 *
 * MotorSafety is enabled by default.
 */
class RobotDriveBase : public MotorSafety
{
 public:
  /**
   * The location of a motor on the robot for the purpose of driving.
   */
  enum MotorType {
    /// Front-left motor.
    kFrontLeft = 0,
    /// Front-right motor.
    kFrontRight = 1,
    /// Rear-left motor.
    kRearLeft = 2,
    /// Rear-right motor.
    kRearRight = 3,
    /// Left motor.
    kLeft = 0,
    /// Right motor.
    kRight = 1,
    /// Back motor.
    kBack = 2
  };

  RobotDriveBase();
  ~RobotDriveBase() override = default;

  RobotDriveBase(RobotDriveBase&&) = default;
  RobotDriveBase& operator=(RobotDriveBase&&) = default;

  void SetRampingValue(int ramping);

  /**
   * Sets the deadband applied to the drive inputs (e.g., joystick values).
   *
   * The default value is 0.02. Inputs smaller than the deadband are set to 0.0
   * while inputs larger than the deadband are scaled from 0.0 to 1.0. See
   * frc::ApplyDeadband().
   *
   * @param deadband The deadband to set.
   */
  void SetDeadband(float deadband);

  /**
   * Configure the scaling factor for using RobotDrive with motor controllers in
   * a mode other than PercentVbus or to limit the maximum Input.
   *
   * @param maxInput Multiplied with the Input percentage computed by the
   *                  drive functions.
   */
  void SetMaxInput(float maxInput);

  /**
   * Configure the scaling factor for using RobotDrive with motor controllers in
   * a mode other than PercentVbus or to limit the minimum Input.
   *
   * @param minInput Multiplied with the Input percentage computed by the
   *                  drive functions.
   */
  void SetMinInput(float minInput);

  /**
   * Configure the scaling factor for using RobotDrive with motor controllers in
   * a mode other than PercentVbus or to limit the maximum output.
   *
   * @param maxOutput Multiplied with the output percentage computed by the
   *                  drive functions.
   */
  void SetMaxOutput(float maxOutput);

  /**
   * Configure the scaling factor for using RobotDrive with motor controllers in
   * a mode other than PercentVbus or to limit the minimum output.
   *
   * @param minOutput Multiplied with the output percentage computed by the
   *                  drive functions.
   */
  void SetMinOutput(float minOutput);
  
  // void SetVoltageRange(float minOutput, float maxOutput);

  void SetSpeedLimit(float limit);

  /**
   * Feed the motor safety object. Resets the timer that will stop the motors if
   * it completes.
   *
   * @see MotorSafetyHelper::Feed()
   */
  void FeedWatchdog();

  void StopMotor() override = 0;

  std::string GetDescription() const override = 0;

 protected:
  /// Default input ramping.
  static constexpr int kDefaultRampingValue = 80;

  /// Default input deadband.
  static constexpr float kDefaultDeadband = 0.05f;

  /// Default maximum output.
  static constexpr float kDefaultMaxOutput = 1.0f;

  // Default speed limit
  static constexpr float kDefaultSpeedLimit = 0.8f;

  /**
   * Renormalize all wheel speeds if the magnitude of any wheel is greater than
   * 1.0.
   */
  // static void Desaturate(std::span<float> wheelSpeeds);

  /// Input ramping.
  float m_rampingValue = kDefaultRampingValue;

  /// Input deadband.
  float m_deadband = kDefaultDeadband;

  /// Maximum output.
  float m_maxOutput = kDefaultMaxOutput;

  /// Speedlimit
  float m_speedLimit = kDefaultSpeedLimit;
};
