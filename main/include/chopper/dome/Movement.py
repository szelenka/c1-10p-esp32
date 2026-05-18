from time import sleep, monotonic_ns
from math import isnan, isinf
from InverseKinematics import Machine
from adafruit_servokit import ServoKit


def constrain(x: float, out_min: float, out_max: float) -> float:
    if out_min <= out_max:
        return max(min(x, out_max), out_min)
    return min(max(x, out_max), out_min)


class Movement(object):
    # track position information for each leg
    angles = [[0], [0], [0]]
    pos = [0, 0, 0]

    # track speed of each leg
    speed = [0, 0, 0]
    speedPrev = [0, 0, 0]

    # track error of each leg
    error = [0, 0]
    errorPrev = [0, 0]

    def __init__(
            self, machine: Machine, kit: ServoKit, controller,
            angle_original: float = 113.4,
            angle_to_step: float = 3200 / 360.0,
            max_history: int = 3,
            _kp: float = 4E-4, _ki: float = 2E-6, _kd: float = 7E-3,
            _ks: int = 20,
            x_offset: float = 500, y_offset: float = 500
                ):
        self.machine = machine
        self.kit = kit
        self.controller = controller
        self.angle_original = angle_original
        self.angle_to_step = angle_to_step
        self.max_history = max_history

        # setup Servos
        for _ in range(0, 3):
            self.kit.servo[_].actuation_range = 180
            self.kit.servo[_].set_pulse_width_range(800, 2200)
            #self.move_servo(_, self.angle_original)
            self.kit.servo[_].angle = self.angle_original

        # X offset for the center position of the touchpad
        self.x_offset = x_offset
        # Y offset for the center position of the touchpad
        self.y_offset = y_offset
        # PID variables
        self.kp = _kp
        self.ki = _ki
        self.kd = _kd
        self.ks = _ks

    def move_servo(self, servo_id: int, angle: float):
        converted_angle = self.controller.convert_reading_to_coordinate(
            angle, raw_min=-150, raw_max=-90, new_min=0, new_max=180
        )
        new_angle = self.kit.servo[servo_id].angle - converted_angle
        new_angle = round(constrain(new_angle, 0, 120), 0)
        new_angle = angle
        self.angles[servo_id].append(new_angle)
        smoothed = round(sum(self.angles[servo_id]) / len(self.angles[servo_id]), 3)
        if len(self.angles[servo_id]) > self.max_history:
            self.angles[servo_id].pop(0)
        #print(servo_id, angle, new_angle, smoothed, len(self.angles[servo_id]))
        #sleep(1)
        if isnan(angle) or isinf(angle):
            return
        new_angle = min(145, max(15,  (angle )))
        #if servo_id == 0:
        #    print((angle, new_angle, ))
        self.kit.servo[servo_id].angle = new_angle

    def move_to(self, hz: float, nx: float, ny: float) -> None:
        """moves/positions the platform with the given parameters"""
        # TODO: calculate servo angle position
        for i in range(0, 3):
            theta = self.machine.theta(i, hz, nx, ny)
            # TODO: why is theta NaN sometimes?

            #self.pos[i] = round(
            #    (
            #            self.angle_original - theta
            #    ) * self.angle_to_step)
            self.pos[i] = (theta)
        # print((self.pos[0], self.pos[1], self.pos[2]))
        # sets target positions
        for i in range(0, 3):
            self.move_servo(i, self.pos[i])

    def PID(self, setpoint_x: float, setpoint_y: float, setpoint_z: float = 4.25) -> None:
        """
        takes in an X and Y setpoint/position and moves the ball to that position
        """
        p = self.controller.get_point()  # measure X and Y positions
        # PID terms for X and Y directions
        out = [0, 0]
        integr = [0, 0]
        deriv = [0, 0]
        # if the controller is detected (the x position will not be 0)
        if self.controller.is_present():
            # calculates PID values
            for i in range(0, 2):
                # sets previous error
                self.errorPrev[i] = self.error[i]
                # sets error aka X or Y ball position
                self.error[i] = (self.x_offset - p.x - setpoint_x) + (self.y_offset - p.y - setpoint_y)
                # calculates the integral of the error
                # (proportional but not equal to the true integral of the error)
                integr[i] += self.error[i] + self.errorPrev[i]
                # calculates the derivative of the error
                # (proportional but not equal to the true derivative of the error)
                deriv[i] = self.error[i] - self.errorPrev[i]
                # checks if the derivative is a real number or infinite
                deriv[i] = 0 if isnan(deriv[i]) or isinf(deriv[i]) else deriv[i]
                # sets output
                out[i] = self.kp * self.error[i] + self.ki * integr[i] + self.kd * deriv[i]
                # constrain output to have a magnitude of 0.25
                #out[i] = constrain(out[i], -0.25, 0.25)

            # calculates stepper motor speeds
            for i in range(0, 3):
                # sets previous speed
                self.speedPrev[i] = self.speed[i]
                # sets current position
                self.speed[i] = self.kit.servo[i].angle
                # calculates the error in the current position and target position
                self.speed[i] = abs(self.speed[i] - self.pos[i]) * self.ks
                # filters speed by preventing it from being over 100 away from last speed
                self.speed[i] = constrain(self.speed[i], self.speedPrev[i] - 200, self.speedPrev[i] + 200)
                # constrains sped from 0 to 1000
                self.speed[i] = constrain(self.speed[i], 0, 1000)
            #print((-out[0], -out[1], ))
            #print((integr[0], integr[1],))
            #print((deriv[0], deriv[1],))
            #print(f"X OUT = {out[0]}   Y OUT = {out[1]}   Speed A: {self.speed[0]}")  # print X and Y outputs

        # continues moving platform and waits until 20 millis has elapsed
        ts = monotonic_ns()
        while monotonic_ns() - ts < 20:
            self.move_to(setpoint_z, -out[0], -out[1])  # moves the platform