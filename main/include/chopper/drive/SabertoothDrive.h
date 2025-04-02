#pragma once

#include "chopper/motorController/SabertoothController.h"
#include <Sabertooth.h>
#include <Bluepad32.h>
#include "SettingsSystem.h"
#include <vector>

class SabertoothDrive {
public:
    SabertoothDrive(byte address, Stream& stream, int numMotors) :
        m_sabertoothDriver(address, stream)
    {
        Console.println("SabertoothDrive [init]");
        m_sabertoothDriver.setBaudRate(SABERTOOTH_SERIAL_BAUD_RATE);

        // Ensure numMotors is positive
        if (numMotors <= 0) {
            Console.println("Invalid number of motors. Setting to 1.");
            numMotors = 1;
        }

        // Initialize SabertoothController objects
        for (int i = 1; i <= numMotors; ++i) {
            m_motors.emplace_back(m_sabertoothDriver, i);
        }
    }

    SabertoothController& GetMotor(int motorId) {
        if (motorId < 1 || motorId > static_cast<int>(m_motors.size())) {
            Console.println("Invalid motor ID. Returning the first motor as fallback.");
            return m_motors[0]; // Return the first motor as a fallback
        }
        return m_motors[motorId - 1];
    }

private:
    Sabertooth m_sabertoothDriver;
    std::vector<SabertoothController> m_motors; // Vector of motor controllers
};