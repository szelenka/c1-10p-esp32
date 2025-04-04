#ifndef __SETTINGS_USER_H__
#define __SETTINGS_USER_H__

 // set to true if the motor should be disabled when no signal from a controller is detected
#define C110P_MOTOR_SAFETY              true

// duration in milliseconds to wait before the motor is disabled
// each message from the controller resets the timer
#define C110P_MOTOR_SAFETY_TIMEOUT_MS   1500    

// setTimeout rounds up to the nearest 100 milliseconds
// A value of 0 disables the serial timeout
#define C110P_MOTOR_SERIAL_TIMEOUT_MS   950     



// Ramp time delay between full forward and full reverse speed:
//       1-10: Fast         = 256/(~1000 * COMMAND_VALUE)
//      11-20: Slow         = 256/(15.25 * (COMMAND_VALUE - 10))
//      21-80: Intermediate = 256/(15.25 * (COMMAND_VALUE - 10))
#define C110P_DRIVE_RAMPING_PERIOD      80      

// Top speed limiter - percentage 0.0 - 1.0
#define C110P_DRIVE_MAXIMUM_SPEED       0.65f

// Scale value of 1 means instant. Scale value of 100 means that the throttle will increase 1/100 every 25ms
#define C110P_DRIVE_ACCELERATION_SCALE  100

// Scale value of 1 means instant. Scale value of 20 means that the throttle will decrease 1/20 every 25ms
#define C110P_DRIVE_DECELERATION_SCALE  20

#define C110P_DRIVE_DEADBAND            0.05


#define C110P_CONTROLLER_FORGET_KEYS            false
#define C110P_CONTROLLER_JOYSTICK_LEFT_OFFSET_X       -30
#define C110P_CONTROLLER_JOYSTICK_LEFT_OFFSET_Y       0


#define C110P_CONTROLLER_JOYSTICK_LEFT_INVERT_X       true
#define C110P_CONTROLLER_JOYSTICK_LEFT_INVERT_Y       true
#define C110P_CONTROLLER_JOYSTICK_LEFT_SLEW_RATE_POSITIVE  0.75
#define C110P_CONTROLLER_JOYSTICK_LEFT_SLEW_RATE_NEGATIVE  -0.75

#define C110P_CONTROLLER_JOYSTICK_RIGHT_OFFSET_X       0
#define C110P_CONTROLLER_JOYSTICK_RIGHT_OFFSET_Y       0

#define C110P_CONTROLLER_JOYSTICK_RIGHT_INVERT_X      true
#define C110P_CONTROLLER_JOYSTICK_RIGHT_INVERT_Y      true
#define C110P_CONTROLLER_JOYSTICK_RIGHT_SLEW_RATE_POSITIVE  0.75
#define C110P_CONTROLLER_JOYSTICK_RIGHT_SLEW_RATE_NEGATIVE  -0.75


// Ramp time delay between full forward and full reverse speed:
//       1-10: Fast         = 256/(~1000 * COMMAND_VALUE)
//      11-20: Slow         = 256/(15.25 * (COMMAND_VALUE - 10))
//      21-80: Intermediate = 256/(15.25 * (COMMAND_VALUE - 10))
#define C110P_DOME_RAMPING_PERIOD      80      

// Dome speed limiter - percentage 0.0 - 1.0
#define C110P_DOME_MAXIMUM_SPEED        0.65f

// Scale value of 1 means instant. Scale value of 100 means that the throttle will increase 1/100 every 25ms
#define C110P_DOME_ACCELERATION_SCALE  100

// Scale value of 1 means instant. Scale value of 20 means that the throttle will decrease 1/20 every 25ms
#define C110P_DOME_DECELERATION_SCALE  20

#define C110P_DOME_DEADBAND            0.05



#define C110P_SCALING                   true    // set to true if acceleration/decelleration should be applied
#define C110P_THROTTLE_INVERTED         false   // set to true if throttle should be inverted
#define C110P_TURN_INVERTED             false   // set to true if turn should be inverted
#define C110P_DOME_INVERTED             false   // set to true if dome drive should be inverted

#define C110P_DRIVE_SYSTEM_ARCADE       0
#define C110P_DRIVE_SYSTEM_CURVE        1
#define C110P_DRIVE_SYSTEM_TANK         2
#define C110P_DRIVE_SYSTEM_REELTWO      3

// Select the drive system to use
#define C110P_DRIVE_SYSTEM              C110P_DRIVE_SYSTEM_ARCADE



#ifndef DOME_DIRECTION_CHANGE_THRESHOLD
#define DOME_DIRECTION_CHANGE_THRESHOLD 5
#endif

#ifndef DOME_RANDOM_MOVE_MIN_DEGREES
#define DOME_RANDOM_MOVE_MIN_DEGREES 5
#endif

#endif
