#pragma once

#include <cmath>
#include <iostream>

class IKMachine {
public:
    IKMachine(double d, double e, double f, double g) : d(d), e(e), f(f), g(g) {
        std::cout << "Machine: d: " << this->d << " e: " << this->e << " f: " << this->f << " g: " << this->g
                  << std::endl;
    }

    double theta(int leg, double hz, double nx, double ny) {
        double nmag = 0.0;                 // magnitude
        double nz = 0.0;                   // z component of the normal vector
        double x = 0.0, y = 0.0, z = 0.0;  // generic variables for the components of leg
        double mag = 0.0;                  // generic magnitude of the leg vector
        double angle = 0.0;                // generic angle for leg

        // create unit normal vector
        nmag = std::sqrt(std::pow(nx, 2) + std::pow(ny, 2) + 1);
        nx /= nmag;
        ny /= nmag;
        nz = 1 / nmag;

        // calculates angle for each leg
        if (leg == 0) {
            x = 0.0;
            y = d + (e / 2) * (1 - (std::pow(nx, 2) + 3 * std::pow(nz, 2) + 3 * nz) / (nz + 1 - std::pow(nx, 2)) +
                               (std::pow(nx, 4) - 3 * std::pow(nx, 2) * std::pow(ny, 2)) /
                                   ((nz + 1) * (nz + 1 - std::pow(nx, 2))));
            z = hz + e * ny;
            mag = std::sqrt(std::pow(y, 2) + std::pow(z, 2));
            angle =
                std::acos(y / mag) - std::acos((std::pow(mag, 2) + std::pow(f, 2) - std::pow(g, 2)) / (2 * mag * f));
        } else if (leg == 1) {
            x = (std::sqrt(3) / 2) * (e * (1 - (std::pow(nx, 2) + std::sqrt(3) * nx * ny) / (nz + 1)) - d);
            y = x / std::sqrt(3);
            z = hz - (e / 2) * (std::sqrt(3) * nx + ny);
            mag = std::sqrt(std::pow(x, 2) + std::pow(y, 2) + std::pow(z, 2));
            angle = std::acos((std::sqrt(3) * x + y) / (-2 * mag)) -
                    std::acos((std::pow(mag, 2) + std::pow(f, 2) - std::pow(g, 2)) / (2 * mag * f));
        } else if (leg == 2) {
            x = (std::sqrt(3) / 2) * (d - e * (1 - (std::pow(nx, 2) - std::sqrt(3) * nx * ny) / (nz + 1)));
            y = -x / std::sqrt(3);
            z = hz + (e / 2) * (std::sqrt(3) * nx - ny);
            mag = std::sqrt(std::pow(x, 2) + std::pow(y, 2) + std::pow(z, 2));
            angle = std::acos((std::sqrt(3) * x - y) / (2 * mag)) -
                    std::acos((std::pow(mag, 2) + std::pow(f, 2) - std::pow(g, 2)) / (2 * mag * f));
        }

        // converts angle to degrees and returns the value
        return angle * rad2deg;
    }

private:
    // radians to degrees conversion factor
    const double rad2deg = 180.0 / M_PI;

    double d, e, f, g;
};
