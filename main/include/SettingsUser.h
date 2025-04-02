#ifndef __SETTINGS_USER_H__
#define __SETTINGS_USER_H__

#define C110P_CONTROLLER_TIMEOUT_MS     1500    // Miliseconds between successful responses from controllers, before emergency-stop
#define C110P_MOTOR_TIMEOUT_MS          950     // Miliseconds between communication from microcontroller, before emergency-stop
#define C110P_RAMPING_PERIOD            80      // Ramp time delay between full forward and full reverse speed:
                                                //       1-10: Fast         = 256/(~1000 * COMMAND_VALUE)
                                                //      11-20: Slow         = 256/(15.25 * (COMMAND_VALUE - 10))
                                                //      21-80: Intermediate = 256/(15.25 * (COMMAND_VALUE - 10))

#define C110P_MAXIMUM_SPEED             0.5f    // Top speed limiter - percentage 0.0 - 1.0. default 50%
// #define C110P_MAXIMUM_GUEST_SPEED       0.3f    // Top speed for a guest joystick (if used) - percentage 0.0 - 1.0. default 30%
#define C110P_ACCELERATION_SCALE        100     // Scale value of 1 means instant. Scale value of 100 means that the throttle will increase 1/100 every 25ms
#define C110P_DECELERATION_SCALE        20      // Scale value of 1 means instant. Scale value of 20 means that the throttle will decrease 1/20 every 25ms
#define C110P_SCALING                   true    // set to true if acceleration/decelleration should be applied
#define C110P_THROTTLE_INVERTED         false   // set to true if throttle should be inverted
#define C110P_TURN_INVERTED             false   // set to true if turn should be inverted
#define C110P_DOME_INVERTED             false   // set to true if dome drive should be inverted

#define C110P_DRIVE_SYSTEM_ARCADE       0
#define C110P_DRIVE_SYSTEM_CURVE        1
#define C110P_DRIVE_SYSTEM_TANK         2
#define C110P_DRIVE_SYSTEM_REELTWO      3

#define C110P_DRIVE_SYSTEM              0       // 0 = Arcade, 1 = Curve, 2 = Tank, 3 = ReelTwo

#ifndef DOME_DIRECTION_CHANGE_THRESHOLD
#define DOME_DIRECTION_CHANGE_THRESHOLD 5
#endif

#ifndef DOME_RANDOM_MOVE_MIN_DEGREES
#define DOME_RANDOM_MOVE_MIN_DEGREES 5
#endif

#endif
