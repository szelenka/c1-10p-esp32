#pragma once

#include "chopper/core/PublishingNode.h"
#include "chopper/core/IControllerSource.h"
#include "chopper/messages/CommonMessages.h"

namespace chopper::examples {

/**
 * @brief Generic controller input node.
 *
 * Reads from any IControllerSource implementation and publishes
 * ControllerInput messages. Completely decoupled from specific
 * controller backends.
 */
class ControllerInputNode : public core::PublishingNode {
public:
    explicit ControllerInputNode(const char* name = "controller_input");

    bool initialize() override;
    void process(uint64_t now) override;
    void emergencyStop() override;
    [[nodiscard]] double getUpdateFrequency() const override { return 50.0; }

    void setControllerSource(core::ControllerSourcePtr controller_source);
    [[nodiscard]] core::ControllerSourcePtr getControllerSource() const { return controller_source_; }
    [[nodiscard]] core::IControllerSource::ConnectionState getConnectionState() const;

private:
    bool hasSignificantChange(const messages::ControllerInput& input);

    core::TypedPublisherPtr<messages::ControllerInput> controller_pub_;
    core::ControllerSourcePtr controller_source_;
    messages::ControllerInput last_input_;
    uint64_t last_publish_time_ = 0;
    static constexpr float ANALOG_THRESHOLD = 0.02f;
    static constexpr uint64_t MIN_PUBLISH_INTERVAL_US = 20000;
};

/**
 * @brief Motor control node.
 * Subscribes to MotorCommand messages and controls Sabertooth motor controllers.
 */
class MotorControlNode : public core::PublishingNode {
public:
    explicit MotorControlNode(const char* name = "motor_control");

    bool initialize() override;
    void process(uint64_t now) override;
    void emergencyStop() override;

    void setDifferentialDrive(class DifferentialDrive* diff_drive);
    void setDomeDrive(class SingleDrive* dome_drive);

private:
    void handleMotorCommand(const messages::MotorCommand& cmd);

    core::TypedSubscriptionPtr<messages::MotorCommand> motor_sub_;
    class DifferentialDrive* diff_drive_;
    class SingleDrive* dome_drive_;
    bool emergency_stopped_;
};

/**
 * @brief Sensor reading node.
 * Reads sensor data and publishes SensorData messages.
 */
class SensorNode : public core::PublishingNode {
public:
    explicit SensorNode(const char* name = "sensor");

    bool initialize() override;
    void process(uint64_t now) override;
    void emergencyStop() override;
    [[nodiscard]] double getUpdateFrequency() const override { return 10.0; }

    void setDomePosition(class DomePosition* dome_sensor);

private:
    core::TypedPublisherPtr<messages::SensorData> sensor_pub_;
    class DomePosition* dome_sensor_;
};

/**
 * @brief Drive control logic node.
 * Subscribes to controller input, publishes motor commands.
 */
class DriveControlNode : public core::PublishingNode {
public:
    explicit DriveControlNode(const char* name = "drive_control");

    bool initialize() override;
    void process(uint64_t now) override;
    void emergencyStop() override;

private:
    void handleControllerInput(const messages::ControllerInput& input);

    core::TypedSubscriptionPtr<messages::ControllerInput> controller_sub_;
    core::TypedPublisherPtr<messages::MotorCommand> motor_pub_;

    float max_drive_speed_;
    float max_dome_speed_;
    bool inverted_steering_;
};

}  // namespace chopper::examples
