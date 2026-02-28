from math import sqrt, pow, acos, pi

class Machine(object):
    # radians to degrees conversion factor
    rad2deg: float = 180 / pi
    def __init__(self, d: float, e: float, f: float, g: float):
        """
        d = distance from the center of the base to any of its corners
        e = distance from the center of the platform to any of its corners
        f = length of link #1
        g = length of link #2
        """
        self.d = d
        self.e = e
        self.f = f
        self.g = g
        print(f"Machine: d: {self.d} e: {self.e} f: {self.f} g: {self.g}")

    def theta(self, leg: int, hz: float, nx: float, ny: float):
        nmag: float = 0.0     # magnitude
        nz: float = 0.0       # z component of the normal vector
        x: float = 0.0
        y: float = 0.0
        z: float = 0.0        # generic variables for the components of leg
        mag: float = 0.0      # generic magnitude of the leg vector
        angle: float = 0.0    # generic angle for leg

        # create unit normal vector
        nmag = sqrt(pow(nx, 2) + pow(ny, 2) + 1)  # magnitude of the normal vector
        nx /= nmag
        ny /= nmag
        nz = 1 / nmag
        #print((nx, ny, nz, ))
        # calculates angle for each leg
        if leg == 0:
            # first leg is on the x-axis
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
            # TODO: is this inverted, since our arm bends in?
            angle = acos(y / mag) - acos(
                (pow(mag, 2) + pow(self.f, 2) - pow(self.g, 2)) / (2 * mag * self.f)
            )
            print((0, ny, nx, hz, x, y, z, mag, angle))

            #print((pow(mag, 2) + pow(self.f, 2) - pow(self.g, 2)) / (2 * mag * self.f))
        elif leg == 1:
            x = (sqrt(3) / 2) * (self.e * (1 - (pow(nx, 2) + sqrt(3) * nx * ny) / (nz + 1)) - self.d)
            y = x / sqrt(3)
            z = hz - (self.e / 2) * (sqrt(3) * nx + ny)
            mag = sqrt(pow(x, 2) + pow(y, 2) + pow(z, 2))
            angle = acos(
                (sqrt(3) * x + y) / (-2 * mag)
            ) - acos(
                (pow(mag, 2) + pow(self.f, 2) - pow(self.g, 2)) / (2 * mag * self.f)
            )
        elif leg == 2:
            x = (sqrt(3) / 2) * (self.d - self.e * (1 - (pow(nx, 2) - sqrt(3) * nx * ny) / (nz + 1)))
            y = -x / sqrt(3)
            z = hz + (self.e / 2) * (sqrt(3) * nx - ny)
            mag = sqrt(pow(x, 2) + pow(y, 2) + pow(z, 2))
            angle = acos(
                (sqrt(3) * x - y) / (2 * mag)
            ) - acos(
                (pow(mag, 2) + pow(self.f, 2) - pow(self.g, 2)) / (2 * mag * self.f)
            )

        #print(nx, ny, x, y, z, mag, angle)
        # converts angle to degrees and returns the value
        return angle * self.rad2deg