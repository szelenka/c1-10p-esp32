from math import sqrt, pow, acos, asin, pi

class Machine(object):
    # radians to degrees conversion factor
    rad2deg: float = 180 / pi

    def __init__(
        self, base_altitude: float, end_effector_altitude: float,
        bottom_link_length: float, top_link_length: float, min_height: float,
        limit_normal_vector: float = 0.25, bend_out: bool = False
    ):
        """
        d = distance from the center of the base to any of its corners
        e = distance from the center of the end-effector to any of its corners
        f = length of link #1
        g = length of link #2
        bend_out = joint for f-g bends out?
        """
        # x-length from center of base triangle to point
        self.d = base_altitude * sqrt(3)/3
        # x-length from center of end-effector triangle to point
        self.e = end_effector_altitude * sqrt(3)/3
        self.f = bottom_link_length
        self.g = top_link_length
        self.bend_out = bend_out
        # z-height from servo center (i.e. base) to connection center (i.e. platform)
        self.min_height = min_height
        self.max_height = sqrt(pow(self.g + self.f, 2) - pow(self.d - self.e, 2))
        self.max_height_angle = self.calc_flat_angle()
        self.min_height_angle = self.calc_min_height_angle()
        self.avg_angle = (self.min_height_angle + self.max_height_angle) / 2.
        self.limit_normal_vector = limit_normal_vector
        print(f"Machine: d: {self.d} e: {self.e} f: {self.f} g: {self.g}")
        print(f"  min_height: {self.min_height} min_height_angle: {self.min_height_angle}")
        print(f"  max_height: {self.max_height} max_height_angle: {self.max_height_angle}")

    def calc_flat_angle(self) -> float:
        """Calculate the Servo Angle for the highest height given the dimensions of the Machine"""
        if self.d > self.e:
            theta = acos((self.d - self.e)/(self.g + self.f))
            ref_angle = 0
        elif self.d < self.e:
            theta = acos(self.max_height/(self.g + self.f))
            ref_angle = 90
        else:
            theta = 0
            ref_angle = 90

        return ref_angle + theta * self.rad2deg

    def calc_min_height_angle(self) -> float:
        """Calculate the Servo Angle at the lowest height given the dimensions of the Machine"""
        c = sqrt(pow(self.d - self.e, 2) + pow(self.min_height, 2))
        if self.bend_out:
            if self.d > self.e:
                theta1 = acos((self.d - self.e)/self.f)
                theta2 = acos(
                    (pow(c, 2) + pow(self.f, 2) - pow(self.g, 2)) / (2 * c * self.f)
                )
                angle = 180 - (theta1 + theta2) * self.rad2deg
            elif self.d < self.e:
                theta1 = acos(self.min_height/c)
                theta2 = acos(
                    (pow(c, 2) + pow(self.f, 2) - pow(self.g, 2)) / (2 * c * self.f)
                )
                angle = 90 + (theta1 + theta2) * self.rad2deg
            else:
                theta1 = 0
                theta2 = acos(
                    (pow(c, 2) + pow(self.f, 2) - pow(self.g, 2)) / (2 * c * self.f)
                )
                angle = 90 + (theta1 + theta2) * self.rad2deg
        else:
            if self.d > self.e:
                theta1 = acos(self.min_height/c)
                theta2 = acos(
                    (pow(c, 2) + pow(self.f, 2) - pow(self.g, 2)) / (2 * c * self.f)
                )
                angle = 90 - (theta1 + theta2) * self.rad2deg
            elif self.d < self.e:
                theta1 = acos((self.e - self.d)/c)
                theta2 = acos(
                    (pow(c, 2) + pow(self.f, 2) - pow(self.g, 2)) / (2 * c * self.f)
                )
                angle = 180 - (theta1 + theta2) * self.rad2deg
            else:
                # c should equal min_height
                theta1 = 0
                theta2 = acos(
                    (pow(c, 2) + pow(self.f, 2) - pow(self.g, 2)) / (2 * c * self.f)
                )
                angle = 90 - (theta1 + theta2) * self.rad2deg

        return angle

    def unit_normal_vector(self, nx: float, ny: float) -> (float, float, float, ):
        # create unit normal vector
        # unit normal vector must be between -1,1 and should likely only step in 0.25 increments?
        # TODO: verify nx/ny are within the valid range
        assert -self.limit_normal_vector <= nx <= self.limit_normal_vector, f"nx out of range: {nx}"
        assert -self.limit_normal_vector <= ny <= self.limit_normal_vector, f"ny out of range: {ny}"
        nmag = sqrt(pow(nx, 2) + pow(ny, 2) + 1)  # magnitude of the normal vector
        nx /= nmag
        ny /= nmag
        nz = 1 / nmag
        #assert nz == 1, f"nz out of range: {nz}"
        return (
            min(self.limit_normal_vector, max(-self.limit_normal_vector, nx)),
            min(self.limit_normal_vector, max(-self.limit_normal_vector, ny)),
            nz,
        )

    def get_leg_angles(self, nx: float, ny: float, hz: float) -> (float, float, float, ):
        nmag: float = 0.0     # magnitude
        nz: float = 0.0       # z component of the normal vector
        x: float = 0.0
        y: float = 0.0
        z: float = 0.0        # generic variables for the components of leg
        mag: float = 0.0      # generic magnitude of the leg vector
        theta1: float = 0.0   # generic angle for triangle 1
        theta2: float = 0.0   # generic theta for triangle 2

        # create unit normal vector
        nx, ny, nz = self.unit_normal_vector(nx, ny)
        hz = min(self.max_height, max(self.min_height, hz))

        # calculates angle for each leg
        leg_angles = [0, 0, 0]
        for _ in range(0, 3):
            if _ == 0:
                x = 0.0
                y = self.d + (self.e / 2) * (
                    1 - (
                        pow(nx, 2) + 3 * pow(nz, 2) + 3 * nz
                    ) / (
                        nz + 1 - pow(nx, 2)
                    ) + (
                            pow(nx, 4) - 3 * pow(nx, 2) * pow(ny, 2)
                        ) / (
                            (nz + 1) * (nz + 1 - pow(nx, 2))
                        )
                    )
                z = hz + self.e * ny
                mag = sqrt(pow(y, 2) + pow(z, 2))
                theta1 = acos(y / mag)
                theta2 = acos(
                    (pow(mag, 2) + pow(self.f, 2) - pow(self.g, 2)) / (2 * mag * self.f)
                )
            elif _ == 1:
                x = (sqrt(3) / 2) * (
                    self.e * (
                        1 - (pow(nx, 2) + sqrt(3) * nx * ny) / (nz + 1)
                    ) - self.d
                )
                y = x / sqrt(3)
                z = hz - (self.e / 2) * (sqrt(3) * nx + ny)
                mag = sqrt(pow(x, 2) + pow(y, 2) + pow(z, 2))
                theta1 = acos(
                    (sqrt(3) * x + y) / (-2 * mag)
                )
                theta2 = acos(
                    (pow(mag, 2) + pow(self.f, 2) - pow(self.g, 2)) / (2 * mag * self.f)
                )
            elif _ == 2:
                x = (sqrt(3) / 2) * (
                    self.d - self.e * (
                        1 - (pow(nx, 2) - sqrt(3) * nx * ny) / (nz + 1)
                    )
                )
                y = -x / sqrt(3)
                z = hz + (self.e / 2) * (sqrt(3) * nx - ny)
                mag = sqrt(pow(x, 2) + pow(y, 2) + pow(z, 2))
                theta1 = acos(
                    (sqrt(3) * x - y) / (2 * mag)
                )
                theta2 = acos(
                    (pow(mag, 2) + pow(self.f, 2) - pow(self.g, 2)) / (2 * mag * self.f)
                )

            if self.bend_out:
                theta = theta1 + theta2
                leg_angles[_] = self.min_height_angle - min(self.min_height_angle, max(self.max_height_angle, theta * self.rad2deg))
            else:
                theta = theta1 - theta2
                leg_angles[_] = min(self.max_height_angle, max(self.min_height_angle, theta * self.rad2deg))

        return leg_angles