#pragma once

#include "chopper/core/Message.h"

namespace chopper {
namespace messages {

/**
 * @brief Controller input message matching ControllerDecorator structure
 */
class ControllerInput : public core::TypedMessage<ControllerInput> {
public:
    ControllerInput() = default;

    // D-pad
    uint8_t dpad = 0;

    // Analog sticks (raw int32_t values as per ControllerDecorator)
    int32_t axis_x = 0;
    int32_t axis_y = 0;
    int32_t axis_rx = 0;
    int32_t axis_ry = 0;

    // Brake & Throttle
    int32_t brake = 0;
    int32_t throttle = 0;

    // Gyro / Accel
    int32_t gyro_x = 0;
    int32_t gyro_y = 0;
    int32_t gyro_z = 0;
    int32_t accel_x = 0;
    int32_t accel_y = 0;
    int32_t accel_z = 0;

    // Button states (raw values)
    uint16_t buttons = 0;
    uint16_t misc_buttons = 0;

    // Individual button states (bool)
    bool button_a = false;
    bool button_b = false;
    bool button_x = false;
    bool button_y = false;
    bool button_l1 = false;
    bool button_l2 = false;
    bool button_r1 = false;
    bool button_r2 = false;
    bool button_thumb_l = false;
    bool button_thumb_r = false;

    // Misc buttons
    bool misc_system = false;
    bool misc_select = false;
    bool misc_start = false;
    bool misc_capture = false;

    // Controller metadata
    uint8_t controller_id = 0;
    uint8_t battery_level = 0;  // 0=unknown, 1=empty, 255=full
    bool is_connected = false;
    bool has_data = false;
    bool is_gamepad = false;
    bool is_mouse = false;
    bool is_balance_board = false;
    bool is_keyboard = false;

    // MAC address as string (e.g., "AA:BB:CC:DD:EE:FF")
    char mac_address[18] = {0};

    // Processed values (normalized -1.0 to 1.0)
    float axis_x_normalized = 0.0f;
    float axis_y_normalized = 0.0f;
    float axis_rx_normalized = 0.0f;
    float axis_ry_normalized = 0.0f;

    // Slew-rate limited values
    float axis_x_slew = 0.0f;
    float axis_y_slew = 0.0f;
};

/**
 * @brief Motor command message
 */
class MotorCommand : public core::TypedMessage<MotorCommand> {
public:
    enum class CommandType {
        SET_SPEED,      ///< Set motor speed
        SET_POSITION,   ///< Set target position
        STOP,           ///< Stop motor
        EMERGENCY_STOP  ///< Emergency stop
    };

    MotorCommand() = default;
    MotorCommand(uint8_t motor_id, CommandType type, float value)
        : motor_id(motor_id), command_type(type), value(value) {}

    uint8_t motor_id = 0;
    CommandType command_type = CommandType::STOP;
    float value = 0.0f;  ///< Speed (-1.0 to 1.0) or position (motor-specific units)
};

/**
 * @brief Servo command message
 */
class ServoCommand : public core::TypedMessage<ServoCommand> {
public:
    enum class CommandType {
        SET_POSITION,   ///< Set servo position
        SET_SPEED,      ///< Set movement speed
        DISABLE,        ///< Disable servo
        ENABLE          ///< Enable servo
    };

    ServoCommand() = default;
    ServoCommand(uint8_t servo_id, CommandType type, float value)
        : servo_id(servo_id), command_type(type), value(value) {}

    uint8_t servo_id = 0;
    CommandType command_type = CommandType::SET_POSITION;
    float value = 0.0f;     ///< Pulse width in microseconds for SET_POSITION; speed units for SET_SPEED
    uint16_t duration_ms = 0;  ///< Movement duration for position commands
};

/**
 * @brief Sensor data message
 */
class SensorData : public core::TypedMessage<SensorData> {
public:
    enum class SensorType {
        ANALOG,
        DIGITAL,
        BATTERY_VOLTAGE,
        POSITION,
        TEMPERATURE,
        ACCELERATION,
        GYROSCOPE
    };

    SensorData() = default;
    SensorData(uint8_t sensor_id, SensorType type, float value)
        : sensor_id(sensor_id), sensor_type(type), value(value) {}

    uint8_t sensor_id = 0;
    SensorType sensor_type = SensorType::ANALOG;
    float value = 0.0f;
    bool is_valid = true;
};

/**
 * @brief System status message
 */
class SystemStatus : public core::TypedMessage<SystemStatus> {
public:
    enum class Status {
        INITIALIZING,
        RUNNING,
        WARNING,
        ERROR,
        EMERGENCY_STOP,
        SHUTDOWN
    };

    SystemStatus() = default;
    explicit SystemStatus(Status status) : system_status(status) {}

    Status system_status = Status::INITIALIZING;
    uint32_t error_code = 0;
    char message[64] = {0};  ///< Optional status message
};

/**
 * @brief Audio command message
 */
class AudioCommand : public core::TypedMessage<AudioCommand> {
public:
    enum class CommandType {
        PLAY_TRACK,
        STOP,
        PAUSE,
        RESUME,
        SET_VOLUME,
        LOOP_TRACK
    };

    AudioCommand() = default;
    AudioCommand(CommandType type, uint16_t track_id = 0, uint8_t volume = 128)
        : command_type(type), track_id(track_id), volume(volume) {}

    CommandType command_type = CommandType::STOP;
    uint16_t track_id = 0;      ///< Track number to play
    uint8_t volume = 128;       ///< Volume level (0-255)
    bool loop = false;          ///< Loop the track
};

/**
 * @brief LED command message
 */
class LEDCommand : public core::TypedMessage<LEDCommand> {
public:
    enum class CommandType {
        SET_COLOR,
        SET_BRIGHTNESS,
        SET_PATTERN,
        TURN_OFF,
        TURN_ON
    };

    struct Color {
        uint8_t red = 0;
        uint8_t green = 0;
        uint8_t blue = 0;
        uint8_t white = 0;
    };

    LEDCommand() = default;
    LEDCommand(CommandType type, uint8_t led_id = 0)
        : command_type(type), led_id(led_id) {}

    CommandType command_type = CommandType::TURN_OFF;
    uint8_t led_id = 0;         ///< LED identifier
    Color color;                ///< RGB(W) color values
    uint8_t brightness = 255;   ///< Brightness (0-255)
    uint8_t pattern_id = 0;     ///< Pattern identifier for animations
};

} // namespace messages
} // namespace chopper