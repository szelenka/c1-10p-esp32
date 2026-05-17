#pragma once

// ============================================================================
// C-compatible defines for use in main.c and other C translation units.
// These mirror the C++ constexpr values in the namespaces below.
// ============================================================================
#define chopper_config_bluetooth_DRIVE_MAC "98:E6:B9:62:6E:58"
#define chopper_config_bluetooth_DOME_MAC "98:E6:B9:5A:CA:74"
#define chopper_config_bluetooth_ANIMATE_MAC "5C:52:1E:FF:6E:A2"
#define chopper_config_bluetooth_CAMERA_MAC "5C:52:1E:FF:51:45"
#define chopper_config_bluetooth_TEMBED_MAC "7C:2C:67:8A:14:0E"
#define chopper_config_bluetooth_FORGET_ON_STARTUP 0

#ifdef __cplusplus

#include <cstdint>

namespace chopper::config {

// ============================================================================
// GPIO Pin Assignments — ESP32-WROOM-32D/U (Penumbra board)
// ============================================================================

namespace pins {
// UART 1 (shared with USB/Console)
constexpr int SERIAL1_RX = 3;  // GPIO3
constexpr int SERIAL1_TX = 1;  // GPIO1

// UART 2 (shared with RS485)
constexpr int SERIAL2_RX = 33;  // GPIO33
constexpr int SERIAL2_TX = 25;  // GPIO25

// UART 3
constexpr int SERIAL3_RX = 16;  // GPIO16
constexpr int SERIAL3_TX = 17;  // GPIO17

// UART 4 (software serial)
constexpr int SERIAL4_RX = 32;  // GPIO32
constexpr int SERIAL4_TX = 4;   // GPIO4

// I2C / PCA9685
constexpr int SCL = 22;
constexpr int SDA = 21;
constexpr int OUTPUT_ENABLE = 27;

// RS485
constexpr int RS485_RTS = 26;

// GPIO near USB host port
constexpr int DOUT13 = 13;
constexpr int DOUT14 = 14;
constexpr int DIN35 = 35;  // input only
constexpr int DIN34 = 34;  // input only

// Device-level pin assignments (C1-10P wiring)
constexpr int UNUSED_PIN = -1;
constexpr int SABERTOOTH_RX = UNUSED_PIN;  // TX-only, matches legacy NOT_A_PIN
constexpr int SABERTOOTH_TX = SERIAL3_RX;  // TX-only via half-duplex
constexpr int MP3TRIGGER_RX = SCL;
constexpr int MP3TRIGGER_TX = SDA;
constexpr int MAESTRO_DOME_RX = DOUT13;
constexpr int MAESTRO_DOME_TX = DOUT14;
constexpr int MAESTRO_BODY_RX = SERIAL4_RX;
constexpr int MAESTRO_BODY_TX = SERIAL4_TX;
constexpr int OPENMV_RX = SERIAL2_RX;
constexpr int OPENMV_TX = SERIAL2_TX;
constexpr int LED_FRONT = OUTPUT_ENABLE;
constexpr int LED_BACK = OUTPUT_ENABLE;
constexpr int DOME_POTENTIOMETER = DIN34;
}  // namespace pins

// ============================================================================
// Serial Baud Rates
// ============================================================================

namespace baud {
constexpr uint32_t SABERTOOTH = 38400;
constexpr uint32_t MAESTRO = 38400;
constexpr uint32_t OPENMV = 115200;
constexpr uint32_t MP3TRIGGER = 9600;
}  // namespace baud

// ============================================================================
// Motor Controller Startup Timing
// ============================================================================

namespace motor_controller {
// SyRen 10 Packetized Serial requires a two-second power-up delay before
// the shared-bus 0xAA autobaud byte. Sabertooth 2x32 baud is configured
// separately in DEScribe and must match baud::SABERTOOTH.
constexpr uint32_t SYREN_AUTOBAUD_READY_DELAY_MS = 2000;
// Match the Dimension Engineering Arduino helper's post-0xAA settle delay
// before sending addressed motor packets on the shared bus.
constexpr uint32_t SYREN_AUTOBAUD_SETTLE_DELAY_MS = 500;
}  // namespace motor_controller

// ============================================================================
// MP3 Trigger Settings
// ============================================================================

namespace sound {
constexpr uint8_t DEFAULT_VOLUME = 0;
constexpr uint8_t VOLUME_LOUDEST = 0;
constexpr uint8_t VOLUME_QUIETEST = 64;
constexpr uint8_t VOLUME_STEP = 8;
constexpr uint32_t MP3TRIGGER_READY_DELAY_MS = 1500;
}  // namespace sound

// ============================================================================
// Device IDs (Sabertooth address, Maestro mini-SSC device number)
// ============================================================================

namespace device_id {
constexpr uint8_t SABERTOOTH_TANK_DRIVE = 129;
constexpr uint8_t SABERTOOTH_DOME_DRIVE = 128;
constexpr uint8_t MAESTRO_BODY = 12;
constexpr uint8_t MAESTRO_DOME = 13;
}  // namespace device_id

// ============================================================================
// Maestro Servo Channel Assignments
// ============================================================================

namespace servo_channel {
// Body servos (on MAESTRO_BODY)
constexpr uint8_t BODY_NECK_A = 0;
constexpr uint8_t BODY_NECK_B = 1;
constexpr uint8_t BODY_NECK_C = 2;
constexpr uint8_t BODY_UTILITY_ARM = 3;
constexpr uint8_t BODY_DOOR_RIGHT = 4;
constexpr uint8_t BODY_DOOR_LEFT = 5;
constexpr uint8_t BODY_CHANNEL_COUNT = 6;

// Dome servos (on MAESTRO_DOME)
constexpr uint8_t DOME_PERISCOPE_LIFT = 0;
constexpr uint8_t DOME_PERISCOPE_SPIN = 1;
constexpr uint8_t DOME_DOOR_RIGHT = 2;
constexpr uint8_t DOME_ARM_RIGHT_LIFT = 3;
constexpr uint8_t DOME_ARM_RIGHT_EXTEND = 4;
constexpr uint8_t DOME_ARM_RIGHT_ROTATE = 5;
constexpr uint8_t DOME_DOOR_LEFT = 6;
constexpr uint8_t DOME_ARM_LEFT_LIFT = 7;
constexpr uint8_t DOME_ARM_LEFT_EXTEND = 8;
constexpr uint8_t DOME_ARM_LEFT_ROTATE = 9;
constexpr uint8_t DOME_CHANNEL_COUNT = 11;
}  // namespace servo_channel

// ============================================================================
// Controller Joystick Hardware Ranges
// ============================================================================

namespace joystick {
constexpr int16_t INPUT_MAX = 512;
constexpr int16_t INPUT_MIN = -512;
constexpr float OUTPUT_MAX = 1.0f;
constexpr float OUTPUT_MIN = -1.0f;
}  // namespace joystick

// ============================================================================
// Bluetooth Controller MAC Addresses
// ============================================================================

namespace bluetooth {
constexpr const char* DRIVE_MAC = "98:E6:B9:62:6E:58";    // JoyCon(L) White
constexpr const char* DOME_MAC = "98:E6:B9:5A:CA:74";     // JoyCon(R) Gray
constexpr const char* ANIMATE_MAC = "5C:52:1E:FF:6E:A2";  // JoyCon(L) Pink
constexpr const char* CAMERA_MAC = "5C:52:1E:FF:51:45";   // JoyCon(R) Green
constexpr const char* TEMBED_MAC = "7C:2C:67:8A:14:0E";   // T-Embed wrist controller
constexpr bool FORGET_ON_STARTUP = false;
}  // namespace bluetooth

// ============================================================================
// Sound Track IDs (tied to files on SD card)
// ============================================================================

namespace sound_track {
constexpr int32_t GRUMBLY01 = 2;
constexpr int32_t OKAYOKAY = 3;
constexpr int32_t OKAYFOLLOWME = 4;
constexpr int32_t GRUMBLY02 = 5;
constexpr int32_t YESIWOULD = 6;
constexpr int32_t GRUMPY03 = 8;
constexpr int32_t NOW = 12;
constexpr int32_t WHATGROAN = 14;
constexpr int32_t WAH3 = 15;
constexpr int32_t TADA = 16;
constexpr int32_t CHATTY = 17;
constexpr int32_t EXTENDEDGRUMBLE = 20;
constexpr int32_t GRUMBLY1 = 21;
constexpr int32_t UHOH = 24;
constexpr int32_t SWRSTINGER = 32;
constexpr int32_t PURR3 = 33;
constexpr int32_t MANDOLORIAN = 254;
constexpr int32_t IMPERIALCAROLBELLS = 255;
}  // namespace sound_track

// ============================================================================
// Drive System Modes (enum-like constants)
// ============================================================================

namespace drive_mode {
constexpr int32_t ARCADE = 0;
constexpr int32_t CURVE = 1;
constexpr int32_t TANK = 2;
constexpr int32_t REELTWO = 3;
}  // namespace drive_mode

}  // namespace chopper::config

#endif  // __cplusplus
