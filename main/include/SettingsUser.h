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


/*
    DRIVE settings
*/
// Ramp time delay between full forward and full reverse speed:
//       1-10: Fast         = 256/(~1000 * COMMAND_VALUE)
//      11-20: Slow         = 256/(15.25 * (COMMAND_VALUE - 10))
//      21-80: Intermediate = 256/(15.25 * (COMMAND_VALUE - 10))
#define C110P_DRIVE_RAMPING_PERIOD      80      

// Top speed limiter - percentage 0.0 - 1.0
#define C110P_DRIVE_MAXIMUM_SPEED       0.25f

#define C110P_DRIVE_DEADBAND            0.05
#define C110P_DRIVE_MOTOR_1_INVERTED   true
#define C110P_DRIVE_MOTOR_2_INVERTED   false

/*
    DOME settings
*/
// Ramp time delay between full forward and full reverse speed:
//       1-10: Fast         = 256/(~1000 * COMMAND_VALUE)
//      11-20: Slow         = 256/(15.25 * (COMMAND_VALUE - 10))
//      21-80: Intermediate = 256/(15.25 * (COMMAND_VALUE - 10))
#define C110P_DOME_RAMPING_PERIOD      80      

// Dome speed limiter - percentage 0.0 - 1.0
#define C110P_DOME_MAXIMUM_SPEED            0.5f
#define C110P_DOME_DEADBAND                 0.05f
#define C110P_DOME_MOTOR_1_INVERTED         false
#define C110P_DOME_SPIN_SLEW_RATE           3.0f

#define DOME_DIRECTION_CHANGE_THRESHOLD 5
#define DOME_RANDOM_MOVE_MIN_DEGREES 5

/*
    CONTROLLER settings
*/
#define C110P_CONTROLLER_DRIVE_OFFSET_X       -30
#define C110P_CONTROLLER_DRIVE_OFFSET_Y       0


#define C110P_CONTROLLER_DRIVE_INVERT_X       true
#define C110P_CONTROLLER_DRIVE_INVERT_Y       false
#define C110P_CONTROLLER_DRIVE_SLEW_RATE_POSITIVE  3.00
#define C110P_CONTROLLER_DRIVE_SLEW_RATE_NEGATIVE  -3.00

#define C110P_CONTROLLER_DOME_OFFSET_X       0
#define C110P_CONTROLLER_DOME_OFFSET_Y       0

#define C110P_CONTROLLER_DOME_INVERT_X      true
#define C110P_CONTROLLER_DOME_INVERT_Y      true
#define C110P_CONTROLLER_DOME_SLEW_RATE_POSITIVE  0.75
#define C110P_CONTROLLER_DOME_SLEW_RATE_NEGATIVE  -0.75


#define C110P_DRIVE_SYSTEM_ARCADE       0
#define C110P_DRIVE_SYSTEM_CURVE        1
#define C110P_DRIVE_SYSTEM_TANK         2
#define C110P_DRIVE_SYSTEM_REELTWO      3

// Select the drive system to use
#define C110P_DRIVE_SYSTEM              C110P_DRIVE_SYSTEM_CURVE


/*
    SOUND settings
*/
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
#define C110P_SOUND_MANDOLORIAN             254
#define C110P_SOUND_IMERIALCAROLBELLS       255

#endif
