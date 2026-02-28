#include "chopper/examples/ExampleNodes.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <cmath>

static const char* TAG = "ControllerInputNode";

namespace chopper {
namespace examples {

ControllerInputNode::ControllerInputNode(const char* name)
    : PublishingNode(name)
    , controller_source_(nullptr)
    , last_publish_time_(0)
{
}

bool ControllerInputNode::initialize() {
    controller_pub_ = createPublisher<messages::ControllerInput>(
        "controller_input",
        core::QoSProfile::sensorData()
    );

    if (!controller_pub_) {
        logError("Failed to create controller input publisher");
        return false;
    }

    logInfo("Initialized successfully");
    return true;
}

void ControllerInputNode::process(uint64_t now) {
    if (!controller_source_ || !controller_pub_) {
        return;
    }

    if (last_publish_time_ > 0 && (now - last_publish_time_) < MIN_PUBLISH_INTERVAL_US) {
        return;
    }

    messages::ControllerInput input;
    bool data_available = controller_source_->getControllerData(input);

    // Handle disconnection — always publish immediately
    if (!data_available &&
        controller_source_->getConnectionState() == core::IControllerSource::ConnectionState::DISCONNECTED) {
        if (last_input_.is_connected) {
            input = messages::ControllerInput{};
            input.is_connected = false;
            controller_pub_->publish(input, now);
            last_input_ = input;
            last_publish_time_ = now;
            logInfo("Published controller disconnection");
        }
        return;
    }

    if (!data_available) {
        return;
    }

    if (hasSignificantChange(input)) {
        controller_pub_->publish(input, now);
        last_input_ = input;
        last_publish_time_ = now;
    }
}

void ControllerInputNode::emergencyStop() {
    if (controller_pub_) {
        messages::ControllerInput zero_input{};
        zero_input.is_connected = false;
        controller_pub_->publish(zero_input);
        last_input_ = zero_input;
        logInfo("Published emergency stop controller input");
    }
}

void ControllerInputNode::setControllerSource(core::ControllerSourcePtr controller_source) {
    controller_source_ = controller_source;

    if (controller_source_) {
        if (!controller_source_->initialize()) {
            logWarning("Failed to initialize controller source");
        }
    }

    last_input_ = messages::ControllerInput{};
    last_publish_time_ = 0;
}

core::IControllerSource::ConnectionState ControllerInputNode::getConnectionState() const {
    if (!controller_source_) {
        return core::IControllerSource::ConnectionState::DISCONNECTED;
    }
    return controller_source_->getConnectionState();
}

bool ControllerInputNode::hasSignificantChange(const messages::ControllerInput& input) {
    if (input.is_connected != last_input_.is_connected ||
        input.has_data != last_input_.has_data) {
        return true;
    }

    if (!input.is_connected) {
        return false;
    }

    if (input.buttons != last_input_.buttons ||
        input.misc_buttons != last_input_.misc_buttons ||
        input.dpad != last_input_.dpad) {
        return true;
    }

    if (std::abs(input.axis_x_slew - last_input_.axis_x_slew) > ANALOG_THRESHOLD ||
        std::abs(input.axis_y_slew - last_input_.axis_y_slew) > ANALOG_THRESHOLD ||
        std::abs(input.axis_rx_slew - last_input_.axis_rx_slew) > ANALOG_THRESHOLD ||
        std::abs(input.axis_ry_slew - last_input_.axis_ry_slew) > ANALOG_THRESHOLD) {
        return true;
    }

    const int32_t brake_threshold = 20;
    const int32_t throttle_threshold = 20;
    if (std::abs(input.brake - last_input_.brake) > brake_threshold ||
        std::abs(input.throttle - last_input_.throttle) > throttle_threshold) {
        return true;
    }

    return false;
}

} // namespace examples
} // namespace chopper
