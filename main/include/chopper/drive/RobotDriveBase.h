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
class RobotDriveBase : public MotorSafety {
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
  void SetDeadband(double deadband);

  /**
   * Configure the scaling factor for using RobotDrive with motor controllers in
   * a mode other than PercentVbus or to limit the maximum Input.
   *
   * @param maxInput Multiplied with the Input percentage computed by the
   *                  drive functions.
   */
  void SetMaxInput(double maxInput);

  /**
   * Configure the scaling factor for using RobotDrive with motor controllers in
   * a mode other than PercentVbus or to limit the minimum Input.
   *
   * @param minInput Multiplied with the Input percentage computed by the
   *                  drive functions.
   */
  void SetMinInput(double minInput);

  /**
   * Configure the scaling factor for using RobotDrive with motor controllers in
   * a mode other than PercentVbus or to limit the maximum output.
   *
   * @param maxOutput Multiplied with the output percentage computed by the
   *                  drive functions.
   */
  void SetMaxOutput(double maxOutput);

  /**
   * Configure the scaling factor for using RobotDrive with motor controllers in
   * a mode other than PercentVbus or to limit the minimum output.
   *
   * @param minOutput Multiplied with the output percentage computed by the
   *                  drive functions.
   */
  void SetMinOutput(double minOutput);
  
  // void SetVoltageRange(double minOutput, double maxOutput);

  void SetSpeedLimit(double limit);

  /**
   * Feed the motor safety object. Resets the timer that will stop the motors if
   * it completes.
   *
   * @see MotorSafetyHelper::Feed()
   */
  void FeedWatchdog();

  double normalizeSensorInput(int rawValue) {
    if (m_maxInput > m_minInput && m_maxOutput > m_minOutput) {
      return (m_maxOutput-m_minOutput)*((rawValue-m_minInput)/(m_maxInput-m_minInput))+(m_minOutput);
    }
    // TODO: we shouldn't get here .. how to raise a "safe" error?
    return rawValue/m_maxInput;
  }

  void StopMotor() override = 0;

  std::string GetDescription() const override = 0;

 protected:
  /// Default input ramping.
  static constexpr int kDefaultRampingValue = 80;

  /// Default input deadband.
  static constexpr double kDefaultDeadband = 0.02;

  /// Default maximum input.
  static constexpr int kDefaultMaxInput = 512;

  /// Default minimum input.
  static constexpr int kDefaultMinInput = -512;

  /// Default maximum output.
  static constexpr double kDefaultMaxOutput = 1.0;

  /// Default minimum output.
  static constexpr double kDefaultMinOutput = -1.0;

  // Default speed limit
  static constexpr double kDefaultSpeedLimit = 0.8;

  /**
   * Renormalize all wheel speeds if the magnitude of any wheel is greater than
   * 1.0.
   */
  // static void Desaturate(std::span<double> wheelSpeeds);

  /// Input ramping.
  double m_rampingValue = kDefaultRampingValue;

  /// Input deadband.
  double m_deadband = kDefaultDeadband;

  /// Maximum input.
  double m_maxInput = kDefaultMaxInput;

  /// Minimum input.
  double m_minInput = kDefaultMinInput;

  /// Maximum output.
  double m_maxOutput = kDefaultMaxOutput;

  /// Minimum output.
  double m_minOutput = kDefaultMinOutput;

  /// Speedlimit
  double m_speedLimit = kDefaultSpeedLimit;
};
