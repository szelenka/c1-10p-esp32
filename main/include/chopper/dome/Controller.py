from time import sleep
from math import floor
import board
import digitalio
#import adafruit_lis3dh
import adafruit_nunchuk



class Point(object):
    def __init__(self, x: float, y: float, z: float):
        self.x = x
        self.y = y
        self.z = z


class Controller(object):
    def __init__(self, i2c, inradius: float, min_z: float, max_z: float, max_samples: int = 30):
        #self.lis3dh = adafruit_lis3dh.LIS3DH_I2C(i2c=i2c, int1=digitalio.DigitalInOut(board.D6))
        self.nunchuck = adafruit_nunchuk.Nunchuk(i2c)
        self.inradius = inradius
        self.x_range = (-self.inradius, self.inradius)
        self.y_range = self.x_range
        self.z_range = (min_z, max_z, )
        self.max_samples = max_samples
        self.smoothed_values = [[0.], [0.], [0.]]
        print(f"Controller inradius: {self.inradius}")


    @staticmethod
    def convert_reading_to_coordinate(
            raw_value: float, raw_min: float = -135, raw_max: float = 135, new_min: float = 0, new_max: float = 100
    ):
        old_range = (raw_max - raw_min)
        new_range = (new_max - new_min)
        new_value = ((raw_value - raw_min) * new_range) / old_range + new_min
        return min(new_max, max(new_min, new_value))

    def is_present(self) -> bool:
        # TODO: return False if lis3dh is not reading correctly?
        return True

    def new_mean(self, idx: int, value: float, coordinate_range: (float, float, )) -> float:
        new_value = round(
            self.convert_reading_to_coordinate(
                value, new_min=coordinate_range[0], new_max=coordinate_range[1]
            ),
            3
        )
        if len(self.smoothed_values[idx]) >= self.max_samples:
            self.smoothed_values[idx].pop(0)

        self.smoothed_values[idx].append(new_value)
        return round(sum(self.smoothed_values[idx]) / float(len(self.smoothed_values[idx])), 3)

    def get_lis3dh(self) -> (float, float, float, ):
        """return current position of controller"""
        try:
            a = self.lis3dh.acceleration
        except OSError as ex:
            return (0, 0, 0, )

        # -5 to +5
        return (a.x, a.y, a.z, )

    def get_nunchuck(self) -> (float, float, float, ):
        x, y = self.nunchuck.joystick
        # 30 to 226
        #print((x, y, ))
        return (128 - x, 128 - y, 0, )

    def get_nunchuck_btn(self) -> (bool, bool):
        return self.nunchuck.buttons.C, self.nunchuck.buttons.Z

    def get_point(self) -> Point:
        """return current position of controller"""
        x, y, z = self.get_nunchuck()

        #print((a.y, a.y, a.z, ))
        x = self.new_mean(0, x, self.x_range)
        y = self.new_mean(1, y, self.y_range)
        # scale Z based on how far away we are from center
        z = self.convert_reading_to_coordinate(
            max(abs(y), abs(x)), raw_min=0, raw_max=self.inradius, new_min=self.z_range[0], new_max=self.z_range[1]
        )
        return Point(x=x, y=y, z=z)