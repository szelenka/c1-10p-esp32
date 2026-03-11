#pragma once

#include <array>
#include <cmath>
#include <algorithm>
#include <tuple>

namespace chopper {
namespace math {

/**
 * 3-RSS parallel manipulator inverse kinematics solver.
 *
 * A 3-RSS (Revolute-Spherical-Spherical) mechanism with 3 legs,
 * each consisting of two links (f, g) connecting a base triangle
 * to an end-effector platform.
 *
 * Given a desired platform tilt (nx, ny) and height (hz),
 * computes the three servo angles needed to achieve that pose.
 *
 * All math is pure — no framework dependencies.
 */
class RSSMachine {
public:
    /**
     * @param base_altitude            Altitude (height) of equilateral base triangle
     * @param end_effector_altitude    Altitude of equilateral end-effector triangle
     * @param bottom_link_length       Length of bottom link (f)
     * @param top_link_length          Length of top link (g)
     * @param min_height               Minimum platform height above base
     * @param limit_normal_vector      Clamp for tilt components (default 0.25)
     * @param bend_out                 true if joint bends outward
     */
    RSSMachine(float base_altitude, float end_effector_altitude, float bottom_link_length, float top_link_length,
               float min_height, float limit_normal_vector = 0.25f, bool bend_out = false)
        : d(base_altitude * std::sqrt(3.0f) / 3.0f)
        , e(end_effector_altitude * std::sqrt(3.0f) / 3.0f)
        , f(bottom_link_length)
        , g(top_link_length)
        , _platformMinHeight(min_height)
        , _limitNormalVector(limit_normal_vector)
        , _jointIsBentOut(bend_out) {
        _platformMaxHeight = std::sqrt(std::pow(g + f, 2) - std::pow(d - e, 2));
        _platformMaxHeightAngle = calculateStraightAngle();
        _platformMinHeightAngle = calculateMinHeightAngle();
    }

    ~RSSMachine() = default;

    float getMinHeight() const { return _platformMinHeight; }
    float getMaxHeight() const { return _platformMaxHeight; }
    float getMinHeightAngle() const { return _platformMinHeightAngle; }
    float getMaxHeightAngle() const { return _platformMaxHeightAngle; }

    /**
     * Calculate servo angle when links are fully extended (maximum height).
     */
    float calculateStraightAngle() {
        float theta = 0.0f;
        if (d > e) {
            theta = std::acos((d - e) / (g + f));
        } else if (d < e) {
            theta = std::acos(_platformMaxHeight / (g + f)) + _90degRad;
        } else {
            theta = _90degRad;
        }
        return theta * _rad2deg;
    }

    /**
     * Calculate servo angle at minimum height.
     */
    float calculateMinHeightAngle() {
        float ref_hypot = std::sqrt(std::pow(d - e, 2) + std::pow(_platformMinHeight, 2));

        float theta2 = std::acos((std::pow(ref_hypot, 2) + std::pow(f, 2) - std::pow(g, 2)) / (2 * ref_hypot * f));

        float theta1 = 0.0f;
        float angle = 0.0f;

        if (_jointIsBentOut) {
            if (d > e)
                theta1 = std::acos((d - e) / ref_hypot);
            else if (d < e)
                theta1 = std::acos(_platformMinHeight / ref_hypot) + _90degRad;
            else
                theta1 = _90degRad;
            angle = (theta1 + theta2) * _rad2deg;
        } else {
            if (d > e)
                theta1 = std::acos((d - e) / ref_hypot);
            else if (d < e)
                theta1 = std::acos(_platformMinHeight / ref_hypot) + _90degRad;
            else
                theta1 = _90degRad;
            angle = (theta1 - theta2) * _rad2deg;
        }
        return angle;
    }

    /**
     * Normalize and clamp a tilt vector (nx, ny) into a unit normal.
     * Returns (nx, ny, nz).
     */
    std::tuple<float, float, float> unitNormalVector(float nx, float ny) {
        float nmag = std::sqrt(nx * nx + ny * ny + 1.0f);
        nx /= nmag;
        ny /= nmag;
        float nz = 1.0f / nmag;
        return {std::clamp(nx, -_limitNormalVector, _limitNormalVector),
                std::clamp(ny, -_limitNormalVector, _limitNormalVector), nz};
    }

    /**
     * Compute the three leg servo angles for a given platform pose.
     *
     * @param nx   Tilt in x (typically from joystick, -1 to 1 range)
     * @param ny   Tilt in y
     * @param hz   Desired platform height
     * @return Array of 3 servo angles in degrees
     */
    std::array<float, 3> getLegAngles(float nx, float ny, float hz) {
        float nz = 0.0f;
        float x = 0.0f, y = 0.0f, z = 0.0f;
        float mag = 0.0f;
        float theta1 = 0.0f, theta2 = 0.0f;

        std::tie(nx, ny, nz) = unitNormalVector(nx, ny);
        hz = std::clamp(hz, _platformMinHeight, _platformMaxHeight);

        std::array<float, 3> leg_angles = {0.0f, 0.0f, 0.0f};
        for (int i = 0; i < 3; ++i) {
            if (i == 0) {
                // Leg A
                x = 0.0f;
                y = d + (e / 2) * (1 - (nx * nx + 3 * nz * nz + 3 * nz) / (nz + 1 - nx * nx) +
                                   (std::pow(nx, 4) - 3 * nx * nx * ny * ny) / ((nz + 1) * (nz + 1 - nx * nx)));
                z = hz + e * ny;
                mag = std::sqrt(y * y + z * z);
                theta1 = std::acos(y / mag);
                theta2 = std::acos((mag * mag + f * f - g * g) / (2 * mag * f));
            } else if (i == 1) {
                // Leg B
                x = (std::sqrt(3.0f) / 2) * (e * (1 - (nx * nx + std::sqrt(3.0f) * nx * ny) / (nz + 1)) - d);
                y = x / std::sqrt(3.0f);
                z = hz - (e / 2) * (std::sqrt(3.0f) * nx + ny);
                mag = std::sqrt(x * x + y * y + z * z);
                theta1 = std::acos((std::sqrt(3.0f) * x + y) / (-2 * mag));
                theta2 = std::acos((mag * mag + f * f - g * g) / (2 * mag * f));
            } else {
                // Leg C
                x = (std::sqrt(3.0f) / 2) * (d - e * (1 - (nx * nx - std::sqrt(3.0f) * nx * ny) / (nz + 1)));
                y = -x / std::sqrt(3.0f);
                z = hz + (e / 2) * (std::sqrt(3.0f) * nx - ny);
                mag = std::sqrt(x * x + y * y + z * z);
                theta1 = std::acos((std::sqrt(3.0f) * x - y) / (2 * mag));
                theta2 = std::acos((mag * mag + f * f - g * g) / (2 * mag * f));
            }

            if (_jointIsBentOut) {
                leg_angles[i] =
                    std::min(_platformMinHeightAngle, std::max(_platformMaxHeightAngle, (theta1 + theta2) * _rad2deg));
            } else {
                leg_angles[i] =
                    std::min(_platformMaxHeightAngle, std::max(_platformMinHeightAngle, (theta1 - theta2) * _rad2deg));
            }
        }
        return leg_angles;
    }

protected:
    float _limitNormalVector;
    float _platformMaxHeightAngle;
    float _platformMinHeightAngle;
    float _platformMinHeight;
    float _platformMaxHeight;

private:
    static constexpr float _rad2deg = 180.0f / static_cast<float>(M_PI);
    static constexpr float _90degRad = 90.0f * static_cast<float>(M_PI) / 180.0f;
    float d, e, f, g;
    bool _jointIsBentOut;
};

}  // namespace math
}  // namespace chopper
