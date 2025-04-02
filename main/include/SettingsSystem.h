#ifndef __SETTINGS_SYSTEM_H__
#define __SETTINGS_SYSTEM_H__

// Sabertooth Settings
#define SABERTOOTH_SERIAL_BAUD_RATE     9600
#define SABERTOOTH_TANK_DRIVE_ID        129
#define SABERTOOTH_DOME_DRIVE_ID        128

// Maestro Settings
#define MAESTRO_SERIAL_BAUD_RATE        9600

// OpemMV Settings
// TODO: is this fast enough for images?
#define OPENMV_SERIAL_BAUD_RATE         115200

// MP3 Trigger Settings
#define MP3TRIGGER_SERIAL_BAUD_RATE     9600
#define MP3TRIGGER_DEFAULT_VOLUME       50

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
