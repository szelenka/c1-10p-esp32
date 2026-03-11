#pragma once

#include <algorithm>
#include <cmath>

namespace chopper::math {

/**
 * Pure-math drive mixing: converts joystick inputs to left/right wheel speeds.
 *
 * Extracted from DifferentialDrive.cpp IK methods. No hardware dependencies.
 * All methods are static and return normalized speeds in [-1.0, 1.0].
 */
struct WheelSpeeds {
    float left;
    float right;
};

/**
 * Arcade drive: forward/backward + rotation.
 * Joystick desaturation keeps wheel speeds in [-1, 1].
 */
inline WheelSpeeds ArcadeDriveIK(float xSpeed, float zRotation, bool squareInputs = true) {
    xSpeed = std::clamp(xSpeed, -1.0f, 1.0f);
    zRotation = std::clamp(zRotation, -1.0f, 1.0f);

    if (squareInputs) {
        xSpeed = std::copysign(xSpeed * xSpeed, xSpeed);
        zRotation = std::copysign(zRotation * zRotation, zRotation);
    }

    float leftSpeed = xSpeed - zRotation;
    float rightSpeed = xSpeed + zRotation;

    float greaterInput = (std::max)(std::abs(xSpeed), std::abs(zRotation));
    float lesserInput = (std::min)(std::abs(xSpeed), std::abs(zRotation));
    if (greaterInput == 0.0f) {
        return {0.0f, 0.0f};
    }
    float saturatedInput = (greaterInput + lesserInput) / greaterInput;
    leftSpeed /= saturatedInput;
    rightSpeed /= saturatedInput;

    return {leftSpeed, rightSpeed};
}

/**
 * Curvature drive: speed scales rotation magnitude.
 * allowTurnInPlace enables point-turns when xSpeed is zero.
 */
inline WheelSpeeds CurvatureDriveIK(float xSpeed, float zRotation, bool allowTurnInPlace = true) {
    xSpeed = std::clamp(xSpeed, -1.0f, 1.0f);
    zRotation = std::clamp(zRotation, -1.0f, 1.0f);

    float leftSpeed = 0.0f;
    float rightSpeed = 0.0f;

    if (allowTurnInPlace) {
        leftSpeed = xSpeed - zRotation;
        rightSpeed = xSpeed + zRotation;
    } else {
        leftSpeed = xSpeed - std::abs(xSpeed) * zRotation;
        rightSpeed = xSpeed + std::abs(xSpeed) * zRotation;
    }

    float maxMagnitude = std::max(std::abs(leftSpeed), std::abs(rightSpeed));
    if (maxMagnitude > 1.0f) {
        leftSpeed /= maxMagnitude;
        rightSpeed /= maxMagnitude;
    }

    return {leftSpeed, rightSpeed};
}

/**
 * ReelTwo drive: polar-coordinate conversion with 45-degree rotation
 * and sqrt(2) scaling for full range in cardinal directions.
 */
inline WheelSpeeds ReelTwoDriveIK(float xSpeed, float zRotation, bool squareInputs = true) {
    xSpeed = std::clamp(xSpeed, -1.0f, 1.0f);
    zRotation = std::clamp(zRotation, -1.0f, 1.0f);

    if (squareInputs) {
        xSpeed = std::copysign(xSpeed * xSpeed, xSpeed);
        zRotation = std::copysign(zRotation * zRotation, zRotation);
    }

    float ray = std::hypot(xSpeed, zRotation);
    float theta = std::atan2(zRotation, xSpeed);
    theta += static_cast<float>(M_PI_4);
    float leftSpeed = ray * std::cos(theta);
    float rightSpeed = ray * std::sin(theta);

    leftSpeed *= std::sqrt(2.0f);
    rightSpeed *= std::sqrt(2.0f);

    float maxMagnitude = std::max(std::abs(leftSpeed), std::abs(rightSpeed));
    if (maxMagnitude > 1.0f) {
        leftSpeed /= maxMagnitude;
        rightSpeed /= maxMagnitude;
    }

    return {leftSpeed, rightSpeed};
}

/**
 * Tank drive: direct left/right control with optional squared inputs.
 */
inline WheelSpeeds TankDriveIK(float leftSpeed, float rightSpeed, bool squareInputs = true) {
    leftSpeed = std::clamp(leftSpeed, -1.0f, 1.0f);
    rightSpeed = std::clamp(rightSpeed, -1.0f, 1.0f);

    if (squareInputs) {
        leftSpeed = std::copysign(leftSpeed * leftSpeed, leftSpeed);
        rightSpeed = std::copysign(rightSpeed * rightSpeed, rightSpeed);
    }

    return {leftSpeed, rightSpeed};
}

}  // namespace chopper::math
