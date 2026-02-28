import board
import busio
from time import sleep
from math import sqrt, isnan, isinf
from adafruit_servokit import ServoKit

from InverseKinematics import Machine as IKMachine
from RRS_Machine import Machine
from Controller import Controller

# share I2C bus
i2c = busio.I2C(board.SCL, board.SDA)

# initialize the servos
kit = ServoKit(i2c=i2c, channels=8)

machine = Machine(
    base_altitude=148.919,
    end_effector_altitude=211.50,
    bottom_link_length=45,
    top_link_length=31,
    min_height=28.621,
    limit_normal_vector=0.25,
    bend_out=True
)
ik = IKMachine(
    d=machine.d,
    e=machine.e,
    f=machine.f,
    g=machine.g
)

controller = Controller(
    i2c, machine.limit_normal_vector, machine.min_height, machine.max_height, max_samples=1
)

for angle in range(int(machine.min_height_angle), int(machine.max_height_angle), 1):
    for _ in range(0, 3):
        if machine.bend_out:
            kit.servo[_].angle = machine.min_height_angle - angle
        else:
            kit.servo[_].angle = angle
    sleep(0.0001)

def map_value(value, from_min, from_max, to_min, to_max):
    # First, normalize the value from the input range to a 0-1 range
    normalized_value = (value - from_min) / (from_max - from_min)

    # Then, scale it to the output range
    mapped_value = normalized_value * (to_max - to_min) + to_min

    return mapped_value

avg_height = (machine.min_height + machine.max_height) / 2
z = avg_height
servo_start_angle = 0
while True:
    btn_c, btn_z = controller.get_nunchuck_btn()
    p = controller.get_point()
    if btn_c:
        z += 10
    elif btn_z:
        z -= 10
    else:
        z = avg_height
    #print((p.x, p.y, z))
    #ik.theta(0, (machine.min_height + machine.max_height) / 2 + 10, 0,0)
    angles = machine.get_leg_angles(p.x, p.y, z)
    corrected_angles = list(map(lambda _: max(machine.max_height_angle, min(180, 180 - _)), angles))
    #print(corrected_angles)
#[180, 178.755, 179.839]
    max_angle = 175
    min_angle = 145
    for servo_id, angle in enumerate(corrected_angles):
        if isnan(angle) or isinf(angle):
            continue
        # TODO: project angle to calibrated servo range
        if servo_id == 0:
            angle = map_value(angle, machine.max_height_angle, machine.min_height_angle, min_angle + 3, 175)
        elif servo_id == 1:
            angle = map_value(angle, machine.max_height_angle, machine.min_height_angle, min_angle - 6, 166)
            #angle = max(min_angle, min(max_angle - 15, angle - 11))
        elif servo_id == 2:
            angle = map_value(angle, machine.max_height_angle, machine.min_height_angle, min_angle + 5, 177)
            #angle = max(min_angle + 10, min(max_angle + 3, angle - 1))
        kit.servo[servo_id].angle = angle #round(angle, 0)
        #print(servo_id, angle)

    sleep(0.01)