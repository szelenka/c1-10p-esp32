// Copyright (c) FIRST and other WPILib contributors.
// Open Source Software; you can modify and/or share it under the terms of
// the WPILib BSD license file in the root directory of this project.

#pragma once

#include <functional>
#include <string>

#include "chopper/drive/RobotDriveBase.h"

class MotorController;

class SingleDrive : public RobotDriveBase {
 public:
    /**
     * Construct a SingleDrive.
     *
     * @param motor Motor setter function.
     */
    SingleDrive(MotorController& motor);

    SingleDrive(std::function<void(double)> motor);

    ~SingleDrive() override = default;

    SingleDrive(SingleDrive&&) = default;
    SingleDrive& operator=(SingleDrive&&) = default;
  
    void ApplySpeedToMotor();
    
    /**
     * Drive method for a single motor.
     *
     * @param speed        The speed at which the motor should run [-1.0..1.0].
     *                     Forward is positive.
     * @param squareInputs If set, decreases the input sensitivity at low speeds.
     */
    void Drive(double speed, bool squareInputs = true);
    static double DriveIK(double speed, bool squareInputs = true);
    
    /**
     * Stops the motor.
     */
    void StopMotor() override;

    /**
     * Gets the description of the drive.
     *
     * @return A string description of the drive.
     */
    std::string GetDescription() const override;

 private:
    std::function<void(double)> m_motor;

    // Used for motor output
    double m_output = 0.0;
};
