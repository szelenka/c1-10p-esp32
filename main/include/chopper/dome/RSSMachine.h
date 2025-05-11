#pragma once

#include <cmath>
#include <iostream>
#include <cassert>
#include <tuple>
#include <vector>
#include <numeric>
#include "include/SettingsSystem.h"

class RSSMachine
{
public:
/*
    A 3-RSS parallel manipulator with 3 legs and 2 links in each leg
    Each leg consists of two revolute joints (allowing rotation around a fixed axis) and one spherical joint (allowing rotation in any direction)

    Parallel Manipulator means the joint(s) are connected with multiple legs which move from a single platform in parallel


          (y,z)   e
            . _________. (center of end-effector)
           /|
        g / |
         /  |
        .   | mag
         \  |
        f \ |
            .█_________. (center of base)
                  d  

    d = distance from the center of the base to any of its corners
    e = distance from the center of the end-effector to any of its corners
    f = length of link #1
    g = length of link #2
    bend_out = joint for f-g bends out; or bends in
*/
    RSSMachine(
        float base_altitude, float end_effector_altitude,
        float bottom_link_length, float top_link_length, float min_height,
        float limit_normal_vector = 0.25f, bool bend_out = false
    ) : 
        // length from center of base triangle to point on the x-axis
        // altitude * sqrt(3)/3 is the circumradius of an equilateral triangle
        d(base_altitude * std::sqrt(3) / 3),
        e(end_effector_altitude * std::sqrt(3) / 3),
        f(bottom_link_length),
        g(top_link_length),
        // minimum height of the end-effector from base as defined by the caller
        _platformMinHeight(min_height),
        _limitNormalVector(limit_normal_vector),
        _jointIsBentOut(bend_out)
    {
        /*
            maximum height of the end-effector from base (before joint alternates)
            Pythagorean theorem:
                b² = c² - a²
                b = sqrt(c² - a²)
            
                  |\
                b | \ c
                  |__\
                    a

            let:
                a = (d - e)
                b = _max_height
                c = (g + f)

            _max_height = sqrt((g + f)² - (d - e)²)
                        .
                        |\
                        | \ g
                        |  \
            _max_height |   .
                        |    \
                        |     \ f
                        |      \
                        .█______. 
                         (d - e)
            Values are squared, so (+/-) is irrelevant for bend in/out
        */
        _platformMaxHeight = std::sqrt(std::pow(g + f, 2) - std::pow(d - e, 2));
        _platformMaxHeightAngle = calculateStraightAngle();
        _platformMinHeightAngle = calculateMinHeightAngle();
    }

    ~RSSMachine() = default;

    void printSettings()
    {
        DEBUG_RSS_MACHINE_PRINTF(
            "RSSMachine: d: %4.2f e: %4.2f f: %4.2f g: %4.2f\n"
            "  min_height: %4.2f min_height_angle: %4.2f\n"
            "  max_height: %4.2f max_height_angle: %4.2f\n",
            d, e, f, g, _platformMinHeight, _platformMinHeightAngle, _platformMaxHeight, _platformMaxHeightAngle
        );
    }

    // """Calculate the Servo Angle for the highest height given the dimensions of the Machine"""
    float calculateStraightAngle()
    {
        float theta = 0.0f;
        if (d > e)
        {
            /*
                Base (d) is larger than Platform (e)
                     __e__.      Platform
                  g /     |
                   /      | _max_height
                f /ϴ___d__.      Base
                    
                  g /|      Platform
                   / | _max_height
                f /ϴ_|      Base
                (d - e)
            */
            theta = std::acos((d - e) / (g + f));
        }
        else if (d < e)
        {
            /*
                Base (d) is smaller than Platform (e)
                add 90 degrees to the angle to account for the bend out
                  ______e_.      Platform
                g \       |
                   \      | _max_height
                  f \ϴ__d_.      Base
                    
                (e - d)
                g \‾‾|      Platform
                   \ | _max_height
                  f \ϴ      Base
            */
            theta = std::acos(_platformMaxHeight / (g + f)) + _90degRad;
        }
        else
        {
            /*
                Base (d) and Platform (e) are the same size
                theta is 90 degrees beccause the bend straight up:
                   ___e_       Platform
                g |     |
                  |     | _max_height
                f |ϴ__d_|      Base
            */
            theta = _90degRad;
        }
        return theta * _rad2deg;
    }

    // """Calculate the Servo Angle at the lowest height given the dimensions of the Machine"""
    float calculateMinHeightAngle()
    {
        /*
            Calculate reference hypotenuse (c), ignoring joint positions for (f) and (g)
            Pythagorean theorem:
                c² = a² + b²
                c = sqrt(a² + b²)

                  a
                 \‾█|
                c \ | b
                   \|

            let:
                a = (d - e)
                b = _min_height
                c = ref_hypot

            ref_hypot = sqrt((d - e)² + _min_height²)

                    (d - e)
                     ___.___e_.  Platform
                     \ █|
            ref_hypot \ | _min_height
                       \|___d_.  Base

            Values are squared, so (+/-) is irrelevant for bend in/out
        */
        float ref_hypot = std::sqrt(std::pow(d - e, 2) + std::pow(_platformMinHeight, 2));
        float theta1 = 0.0f;
        /*
            Law of cosines in oblique triangles:
                c² = a² + b² - 2ab * cos(C)
                cos(C) = (a² + b² - c²) / 2ab
                C = acos((a² + b² - c²) / 2ab)
                
                  C
                 /\
              b /  \ a
               /    \
            A /______\ B
                 c

            let:
                a = ref_hypot
                b = f
                c = g
                C = θ₂

            θ₂ = acos((ref_hypot² + f² - g²) / (2 * ref_hypot * f))

                  θ₂
                 /\
              f /  \ ref_hypot
               /    \
            A /______\ B
                  g

            redrawn to align with RSS Parallel Manipulator:

                  B______e_.      Platform
                 /|        |
              g / |        | _min_height
               /  |        |
            A /   |        |
              \    | ref_hypot
               \   |       |
              f \  |       |
                  \θ₂____d_.       Base
        */
        float theta2 = std::acos(
            (std::pow(ref_hypot, 2) + std::pow(f, 2) - std::pow(g, 2)) / (2 * ref_hypot * f));
        float angle = 0.0f;

        if (_jointIsBentOut)
        {
            if (d > e)
            {
            /*
                Base is larger than Platform
                              
                              /|      Platform
                             / |
                ref_hypot   /  | _min_height
                           /ϴ₁_|      Base
                         (d - e)
            */
                theta1 = std::acos((d - e) / ref_hypot);
            }
            else if (d < e)
            {
                /*
                    Base is smaller than Platform

                           (e - d)
                             ____      Platform
                             \  |
                    ref_hypot \ | _min_height
                               \ϴ₁     Base
                    
                    Add 90 degrees to the angle to account for the bend out
                */
                theta1 = std::acos(_platformMinHeight / ref_hypot) + _90degRad;
            }
            else
            {
                /*
                Base and Platform are the same size
                theta is 90 degrees beccause the bend straight up:
                            ___e_
                           |     |
                ref_hypot  |     | _min_height
                           |ϴ₁_d_|
                */
                theta1 = _90degRad;
            }
            // theta1 goes from base to ref_hypot, and we need go out to theta2
            // when bent out, theta2 is always beyond theta1; so add
            angle = (theta1 + theta2) * _rad2deg;
        }
        else
        {
            if (d > e)
            {
                /*
                    Base is larger than Platform
                    
                              /|      Platform
                             / |
                ref_hypot   /  | _min_height
                           /ϴ₁_|      Base
                         (d - e)
                */
                theta1 = std::acos((d - e) / ref_hypot);
                
            }
            else if (d < e)
            {
                /*
                    Base is smaller than Platform

                           (e - d)
                             ____      Platform
                             \  |
                    ref_hypot \ | _min_height
                               \ϴ₁     Base
                    
                    Add 90 degrees to the angle to account for the bend out
                */
                theta1 = std::acos(_platformMinHeight / ref_hypot) + _90degRad;
            }
            else
            {
                /*
                Base and Platform are the same size
                theta is 90 degrees beccause the bend straight up:
                            ___e_
                           |     |
                ref_hypot  |     | _min_height
                           |ϴ₁_d_|
                */
                theta1 = _90degRad;
            }
            // theta1 goes from base to ref_hypot, but we need go back to theta2
            // when bent in, theta1 is always beyond theta2; so subtract
            angle = (theta1 - theta2) * _rad2deg;
        }
        return angle;
    }

    std::tuple<float, float, float> unitNormalVector(float nx, float ny)
    {
        /*
            Unit Normal Vector
            A unit normal vector is a vector that is perpendicular to a surface and has a magnitude of 1.0f
                |n| = sqrt(nx² + ny² + nz²).

                   |\
                nz | \ ny
                   |__\
                    nx

            let:
                nz = 1

            Vectors are normalized to have a magnitude (or length) of 1. 
            This is useful because normalized vectors retain their direction but are easier to work with mathematically.
            To normalize a vector v with components (nx, ny, nz), you divide each component by the vector's magnitude
        */
        float nmag = std::sqrt(std::pow(nx, 2.0f) + std::pow(ny, 2.0f) + 1.0f);
        nx /= nmag;
        ny /= nmag;
        float nz = 1.0f / nmag;

        return {
            std::clamp(nx, -_limitNormalVector, _limitNormalVector),
            std::clamp(ny, -_limitNormalVector, _limitNormalVector),
            nz
        };
    }

    std::array<float, 3> getLegAngles(float nx, float ny, float hz)
    {
        float nmag = 0.0f;      // magnitude
        float nz = 0.0f;        // z component of the normal vector
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;         // generic variables for the components of leg
        float mag = 0.0f;       // generic magnitude of the leg vector
        float theta1 = 0.0f;    // generic angle for triangle 1
        float theta2 = 0.0f;    // generic theta for triangle 2

        std::tie(nx, ny, nz) = unitNormalVector(nx, ny);
        hz = std::clamp(hz, _platformMinHeight, _platformMaxHeight);
        DEBUG_RSS_MACHINE_PRINTF("getLegAngles: nx: %4.2f ny: %4.2f hz: %4.2f\n", nx, ny, hz);

        std::array<float, 3> leg_angles({0.0f, 0.0f, 0.0f});
        for (int i = 0; i < 3; ++i)
        {
            if (i == 0) 
            {
                // Leg A
                x = 0.0f;
                y = d + (e / 2) * (
                    1 - (
                        std::pow(nx, 2) + 3 * std::pow(nz, 2) + 3 * nz
                    ) / (
                        nz + 1 - std::pow(nx, 2)
                    ) + (
                        std::pow(nx, 4) - 3 * std::pow(nx, 2) * std::pow(ny, 2)
                    ) / (
                        (nz + 1) * (nz + 1 - std::pow(nx, 2))
                    )
                );
                z = hz + e * ny;
                // pythagorean theorem
                mag = std::sqrt(std::pow(y, 2) + std::pow(z, 2));
                theta1 = std::acos(y / mag);
                // law of cosines
                theta2 = std::acos(
                    (std::pow(mag, 2) + std::pow(f, 2) - std::pow(g, 2)) / (2 * mag * f)
                );
            }
            else if (i == 1)
            {
                // Leg B
                x = (std::sqrt(3) / 2) * (
                    e * (
                        1 - (std::pow(nx, 2) + std::sqrt(3) * nx * ny) / (nz + 1)
                    ) - d
                );
                y = x / std::sqrt(3);
                z = hz - (e / 2) * (std::sqrt(3) * nx + ny);
                // pythagorean theorem
                mag = std::sqrt(std::pow(x, 2) + std::pow(y, 2) + std::pow(z, 2));
                theta1 = std::acos(
                    (std::sqrt(3) * x + y) / (-2 * mag)
                );
                // law of cosines
                theta2 = std::acos(
                    (std::pow(mag, 2) + std::pow(f, 2) - std::pow(g, 2)) / (2 * mag * f)
                );
            }
            else if (i == 2)
            {
                // Leg C
                x = (std::sqrt(3) / 2) * (
                    d - e * (
                        1 - (std::pow(nx, 2) - std::sqrt(3) * nx * ny) / (nz + 1)
                    )
                );
                y = -x / std::sqrt(3);
                z = hz + (e / 2) * (std::sqrt(3) * nx - ny);
                // pythagorean theorem
                mag = std::sqrt(std::pow(x, 2) + std::pow(y, 2) + std::pow(z, 2));
                theta1 = std::acos(
                    (std::sqrt(3) * x - y) / (2 * mag)
                );
                // law of cosines
                theta2 = std::acos(
                    (std::pow(mag, 2) + std::pow(f, 2) - std::pow(g, 2)) / (2 * mag * f)
                );
            }

            if (_jointIsBentOut)
            {
                // when bending out:
                //   _min_height_angle is the max angle
                //   _max_height_angle is the min angle
                leg_angles[i] = std::min(
                    _platformMinHeightAngle, 
                    std::max(_platformMaxHeightAngle, (theta1 + theta2) * _rad2deg));
            }
            else
            {
                // when bending in:
                //   _min_height_angle is the min angle
                //   _max_height_angle is the max angle
                leg_angles[i] = std::min(
                    _platformMaxHeightAngle, 
                    std::max(_platformMinHeightAngle, (theta1 - theta2) * _rad2deg));
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
    // radians to degrees conversion factor
    const float _rad2deg = 180.0f / M_PI;
    const float _90degRad = 90.0f * M_PI / 180.0f;
    float d, e, f, g;
    bool _jointIsBentOut;
};
