#include "chopper/adapters/Bluepad32ControllerSource.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <cstring>

static const char* const TAG = "Bluepad32ControllerSource";

namespace chopper::adapters {

Bluepad32ControllerSource::Bluepad32ControllerSource()
    : controller_(nullptr)
    , initialized_(false)
    , cached_state_(ConnectionState::DISCONNECTED)
    , last_state_check_time_(0) {}

Bluepad32ControllerSource::Bluepad32ControllerSource(std::shared_ptr<ControllerDecorator> controller_decorator)
    : controller_(controller_decorator)
    , initialized_(false)
    , cached_state_(ConnectionState::DISCONNECTED)
    , last_state_check_time_(0) {}

bool Bluepad32ControllerSource::getControllerData(messages::ControllerInput& output) {
    if (!controller_ || getConnectionState() != ConnectionState::CONNECTED) {
        // Return a zero-initialized message for disconnected state
        output = messages::ControllerInput{};
        output.is_connected = false;
        return false;
    }

    // Only proceed if there's new data
    if (!hasNewData()) {
        return false;
    }

    convertControllerData(output);
    return true;
}

core::IControllerSource::ConnectionState Bluepad32ControllerSource::getConnectionState() const {
    // Cache the connection state to avoid repeated expensive checks
    uint64_t now = esp_timer_get_time();
    if (now - last_state_check_time_ < STATE_CACHE_TIME_US) {
        return cached_state_;
    }

    last_state_check_time_ = now;

    if (!controller_) {
        cached_state_ = ConnectionState::DISCONNECTED;
    } else if (!controller_->isConnected()) {
        cached_state_ = ConnectionState::DISCONNECTED;
    } else if (!controller_->hasData()) {
        cached_state_ = ConnectionState::CONNECTING;
    } else {
        cached_state_ = ConnectionState::CONNECTED;
    }

    return cached_state_;
}

bool Bluepad32ControllerSource::hasNewData() const {
    if (!controller_) {
        return false;
    }

    return controller_->isReady();  // This checks both connection and hasData()
}

std::string Bluepad32ControllerSource::getControllerInfo() const {
    if (!controller_) {
        return "No controller";
    }

    std::string info = controller_->getModelName().c_str();
    info += " (Index: " + std::to_string(controller_->index()) + ")";

    if (controller_->isConnected()) {
        auto props = controller_->getProperties();
        char mac_str[18];
        snprintf(mac_str, sizeof(mac_str), "%02X:%02X:%02X:%02X:%02X:%02X", props.btaddr[0], props.btaddr[1],
                 props.btaddr[2], props.btaddr[3], props.btaddr[4], props.btaddr[5]);
        info += " MAC: " + std::string(mac_str);
    }

    return info;
}

uint32_t Bluepad32ControllerSource::getCapabilities() const {
    if (!controller_) {
        return 0;
    }

    return determineCapabilities();
}

std::string Bluepad32ControllerSource::getUniqueId() const {
    if (!controller_) {
        return "no_controller";
    }

    auto props = controller_->getProperties();
    char mac_str[18];
    snprintf(mac_str, sizeof(mac_str), "%02X:%02X:%02X:%02X:%02X:%02X", props.btaddr[0], props.btaddr[1],
             props.btaddr[2], props.btaddr[3], props.btaddr[4], props.btaddr[5]);

    return std::string(mac_str);
}

bool Bluepad32ControllerSource::initialize() {
    if (initialized_) {
        return true;
    }

    // Bluepad32 initialization is handled globally in the main application
    // This adapter just wraps existing functionality
    ESP_LOGI(TAG, "Initialized Bluepad32 controller source");
    initialized_ = true;
    return true;
}

void Bluepad32ControllerSource::shutdown() {
    if (controller_ && controller_->isConnected()) {
        controller_->disconnect();
    }

    controller_.reset();
    initialized_ = false;
    ESP_LOGI(TAG, "Shutdown Bluepad32 controller source");
}

bool Bluepad32ControllerSource::setControllerOutput(uint8_t red, uint8_t green, uint8_t blue, float rumble_strength) {
    if (!controller_ || !controller_->isConnected()) {
        return false;
    }

    try {
        // Set LED color if supported
        controller_->setColorLED(red, green, blue);

        // Set rumble if supported
        if (rumble_strength > 0.0f) {
            uint8_t magnitude = static_cast<uint8_t>(rumble_strength * 255);
            controller_->playDualRumble(0, 100, magnitude, magnitude);  // 100ms rumble
        }

        return true;
    } catch (...) {
        ESP_LOGW(TAG, "Failed to set controller output");
        return false;
    }
}

void Bluepad32ControllerSource::setControllerDecorator(std::shared_ptr<ControllerDecorator> controller_decorator) {
    controller_ = controller_decorator;

    // Reset cached state when controller changes
    cached_state_ = ConnectionState::DISCONNECTED;
    last_state_check_time_ = 0;

    if (controller_) {
        ESP_LOGI(TAG, "Set controller: %s", getControllerInfo().c_str());
    }
}

void Bluepad32ControllerSource::convertControllerData(messages::ControllerInput& output) {
    if (!controller_) {
        return;
    }

    // Use the safe public interface of ControllerDecorator
    // This is slightly less efficient but maintains proper encapsulation
    convertControllerDataSafe(output);
}

void Bluepad32ControllerSource::convertControllerDataSafe(messages::ControllerInput& output) const {
    // Safe fallback using ControllerDecorator's public interface
    // This is less efficient but always works

    output.controller_id = controller_->index();
    output.battery_level = controller_->battery();
    output.is_connected = controller_->isConnected();
    output.has_data = controller_->hasData();
    output.is_gamepad = controller_->isGamepad();
    output.is_mouse = controller_->isMouse();
    output.is_balance_board = controller_->isBalanceBoard();
    output.is_keyboard = controller_->isKeyboard();

    strncpy(output.mac_address, getUniqueId().c_str(), sizeof(output.mac_address) - 1);

    output.dpad = controller_->dpad();
    output.axis_x = controller_->axisX();
    output.axis_y = controller_->axisY();
    output.axis_rx = controller_->axisRX();
    output.axis_ry = controller_->axisRY();
    output.brake = controller_->brake();
    output.throttle = controller_->throttle();
    output.buttons = controller_->buttons();
    output.misc_buttons = controller_->miscButtons();

    output.gyro_x = controller_->gyroX();
    output.gyro_y = controller_->gyroY();
    output.gyro_z = controller_->gyroZ();
    output.accel_x = controller_->accelX();
    output.accel_y = controller_->accelY();
    output.accel_z = controller_->accelZ();

    // Individual buttons
    output.button_a = controller_->a().isPressed();
    output.button_b = controller_->b().isPressed();
    output.button_x = controller_->x().isPressed();
    output.button_y = controller_->y().isPressed();
    output.button_l1 = controller_->l1().isPressed();
    output.button_l2 = controller_->l2().isPressed();
    output.button_r1 = controller_->r1().isPressed();
    output.button_r2 = controller_->r2().isPressed();
    output.button_thumb_l = controller_->thumbL().isPressed();
    output.button_thumb_r = controller_->thumbR().isPressed();

    output.misc_system = controller_->miscSystem();
    output.misc_select = controller_->miscSelect();
    output.misc_start = controller_->miscStart();
    output.misc_capture = controller_->miscCapture();

    // Processed values
    output.axis_x_normalized = controller_->normalizeInput(output.axis_x);
    output.axis_y_normalized = controller_->normalizeInput(output.axis_y);
    output.axis_rx_normalized = controller_->normalizeInput(output.axis_rx);
    output.axis_ry_normalized = controller_->normalizeInput(output.axis_ry);

    output.axis_x_slew = controller_->axisXslew();
    output.axis_y_slew = controller_->axisYslew();
    output.axis_rx_slew = output.axis_rx_normalized;
    output.axis_ry_slew = output.axis_ry_normalized;
}

uint32_t Bluepad32ControllerSource::determineCapabilities() const {
    uint32_t capabilities = 0;

    if (controller_->isGamepad()) {
        capabilities |= HAS_ANALOG_STICKS;
        capabilities |= HAS_TRIGGERS;
    }

    // Most modern gamepads have these features
    auto props = controller_->getProperties();
    if (props.flags & ARDUINO_PROPERTY_FLAG_RUMBLE) {
        capabilities |= HAS_RUMBLE;
    }
    if (props.flags & ARDUINO_PROPERTY_FLAG_PLAYER_LIGHTBAR) {
        capabilities |= HAS_LED;
    }

    // Battery status is available if connected
    if (controller_->isConnected()) {
        capabilities |= HAS_BATTERY_STATUS;
    }

    // Gyro/Accel detection could be improved by checking actual data
    // For now, assume most modern controllers have them
    if (controller_->getClass() == UNI_CONTROLLER_CLASS_GAMEPAD) {
        capabilities |= HAS_GYRO;
        capabilities |= HAS_ACCELEROMETER;
    }

    return capabilities;
}

}  // namespace chopper::adapters
