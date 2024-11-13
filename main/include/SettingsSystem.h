#ifndef __SETTINGS_SYSTEM_H__
#define __SETTINGS_SYSTEM_H__

// Drive System options

#define DRIVE_SYSTEM_PWM                1
#define DRIVE_SYSTEM_SABER              2
#define DRIVE_SYSTEM_ROBOTEQ_PWM        3
#define DRIVE_SYSTEM_ROBOTEQ_SERIAL     4
#define DRIVE_SYSTEM_ROBOTEQ_PWM_SERIAL 5

// Dome System options

#define DOME_DRIVE_NONE                 0
#define DOME_DRIVE_PWM                  1
#define DOME_DRIVE_SABER                2

// Sabertooth Settings
#define SABERTOOTH_SERIAL_BAUD_RATE     9600

// Maestro Settings
#define MAESTRO_SERIAL_BAUD_RATE        9600

#define USE_MOTOR_DEBUG                 true
#define USE_DOME_DEBUG                  true
#define USE_VERBOSE_DOME_DEBUG          true

// Debug statements

#include <Bluepad32.h>
#define DEBUG_PRINTLN(s) Console.println(s)
#define DEBUG_PRINT(s) Console.print(s)
#define DEBUG_PRINTF(...) Console.printf(__VA_ARGS__)
#define DEBUG_PRINTLN_HEX(s) Console.println(s,HEX)
#define DEBUG_PRINT_HEX(s) Console.print(s,HEX)
#define DEBUG_FLUSH() Console.flush()
#endif
