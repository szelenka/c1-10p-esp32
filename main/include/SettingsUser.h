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



// Per the VS1053 datasheet:
// maximum volume is 0x00 (0)
// values much above 0x40 (64) are too low to be audible
#define C110P_SOUND_VOLUME              32

// TODO: create a list of all music file to make the code easier to read
// Document what exists on the SD card in the MP3 Trigger
#define C110P_SOUND_GRUMBLY01               2   
#define C110P_SOUND_OKAYOKAY                3
#define C110P_SOUND_OKAYFOLLOWME            4
#define C110P_SOUND_GRUMBLY02               5
#define C110P_SOUND_YESIWOULD               6
#define C110P_SOUND_GRUMPY03                8
#define C110P_SOUND_NOW                     12
#define C110P_SOUND_WHATGROAN               14
#define C110P_SOUND_3WAH                    15
#define C110P_SOUND_TADA                    16
#define C110P_SOUND_CHATTY                  17
#define C110P_SOUND_EXTENDEDGRUMBLE         20
#define C110P_SOUND_GRUMBLY1                21
#define C110P_SOUND_UHOH                    24
#define C110P_SOUND_SWRSTINGER              32
#define C110P_SOUND_PURR3                   33

#ifndef DOME_DIRECTION_CHANGE_THRESHOLD
#define DOME_DIRECTION_CHANGE_THRESHOLD 5
#endif

#ifndef DOME_RANDOM_MOVE_MIN_DEGREES
#define DOME_RANDOM_MOVE_MIN_DEGREES 5
#endif

#endif
