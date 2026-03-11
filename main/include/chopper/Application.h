#pragma once

#include "chopper/core/Executor.h"
#include "chopper/core/TimerManager.h"
#include "chopper/safety/SafetyManager.h"
#include "chopper/bluetooth/ControllerManager.h"
#include "chopper/hal/DriverManager.h"
#include "chopper/hal/IMotorDriver.h"
#include "chopper/hal/IServoController.h"
#include "chopper/hal/IAudioDriver.h"
#include "chopper/nodes/MotorBridgeNode.h"
#include "chopper/nodes/ServoBridgeNode.h"
#include "chopper/nodes/AudioBridgeNode.h"
#include "chopper/nodes/SafetyNode.h"
#include "chopper/nodes/TelemetryNode.h"
#include "chopper/nodes/TelemetryIOTapNode.h"
#include "chopper/telemetry/TelemetryService.h"

namespace chopper {

/**
 * Top-level application class that wires together all subsystems.
 *
 * Owns the Executor, SafetyManager, ControllerManager, and all bridge nodes.
 * Provides the single entry point for system initialization and startup.
 *
 * Usage:
 *   Application app;
 *   app.setDriveMotor(0, &leftMotor, "drive/cmd");
 *   app.setDriveMotor(1, &rightMotor, "drive/cmd");
 *   app.setDomeMotor(&domeMotor);
 *   app.setBodyServos(&maestroBody);
 *   app.setDomeServos(&maestroDome);
 *   app.setAudio(&mp3);
 *   app.init();
 *   app.start();
 */
class Application {
public:
    static constexpr uint8_t kMaxMotors = 4;

    explicit Application(const core::Executor::Config& config = core::Executor::Config{});

    // -- Hardware registration (call before init) --

    /**
     * Register a motor driver for a specific motor_id on a given topic.
     * @param motor_id  Motor ID that commands are filtered by.
     * @param driver    Motor driver instance (caller owns lifetime).
     * @param topic     Topic to subscribe to (default: "drive/cmd").
     * @return true if registered successfully.
     */
    bool addMotor(uint8_t motor_id, hal::IMotorDriver* driver, const char* topic = "drive/cmd");

    /**
     * Register a servo controller on a given topic.
     * @param controller  Servo controller (caller owns lifetime).
     * @param topic       Topic to subscribe to (default: "servo/cmd").
     * @return true if registered successfully.
     */
    bool addServoController(hal::IServoController* controller, const char* topic = "servo/cmd");

    /**
     * Register an audio driver.
     * @param driver  Audio driver (caller owns lifetime).
     * @param topic   Topic to subscribe to (default: "audio/cmd").
     * @return true if registered successfully.
     */
    bool addAudio(hal::IAudioDriver* driver, const char* topic = "audio/cmd");

    /**
     * Add a custom node to the executor.
     */
    bool addNode(core::NodePtr node);

    /**
     * Configure telemetry output channels.
     * - Serial output is prefixed with "TEL:".
     * - HTTP endpoint (if enabled in build): /api/telemetry
     * - WebSocket endpoint (if enabled in build): /ws/telemetry
     */
    void configureTelemetry(const telemetry::TelemetryService::Config& config) {
        telemetry_config_ = config;
        telemetry_enabled_ = true;
    }

    void disableTelemetry() { telemetry_enabled_ = false; }

    // -- Lifecycle --

    /**
     * Initialize all subsystems:
     *   1. Wire e-stop chain (Executor → SafetyManager)
     *   2. Create bridge nodes for registered hardware
     *   3. Add SafetyNode
     *   4. Initialize all nodes via Executor
     *   5. Initialize all drivers via DriverManager
     */
    bool init();

    /**
     * Start the executor (begins FreeRTOS task).
     * Activates all nodes and starts the main loop.
     */
    bool start();

    /**
     * Stop the executor and shut down drivers.
     */
    void stop();

    /**
     * Trigger an emergency stop from any external source.
     */
    void emergencyStop(const char* reason);
    /**
     * Trigger a soft-stop from any external source:
     * - command all active nodes to safe outputs
     * - force SAFE_STOP degradation mode
     * - keep executor running
     */
    void softStop(const char* reason);
    void clearSoftStop(const char* reason);

    // -- Accessors --

    core::Executor& getExecutor() { return executor_; }
    safety::SafetyManager& getSafetyManager() { return safetyManager_; }
    bluetooth::ControllerManager& getControllerManager() { return controllerManager_; }
    bool isRunning() const { return executor_.isRunning(); }
    bool isInitialized() const { return initialized_; }
    telemetry::TelemetryService& getTelemetryService() { return telemetry_service_; }

private:
    /// Drive all registered actuators to a safe idle state at boot.
    void applyStartupSafeState();

    core::Executor executor_;
    safety::SafetyManager safetyManager_;
    bluetooth::ControllerManager controllerManager_;
    bluetooth::DefaultDisconnectHandler disconnectHandler_;

    // Bridge node storage
    struct MotorRegistration {
        uint8_t motor_id;
        hal::IMotorDriver* driver;
        const char* topic;
    };

    MotorRegistration motors_[kMaxMotors] = {};
    uint8_t motorCount_ = 0;

    struct ServoRegistration {
        hal::IServoController* controller;
        const char* topic;
    };

    static constexpr uint8_t kMaxServos = 4;
    ServoRegistration servos_[kMaxServos] = {};
    uint8_t servoCount_ = 0;

    hal::IAudioDriver* audioDriver_ = nullptr;
    const char* audioTopic_ = nullptr;

    bool initialized_ = false;
    bool telemetry_enabled_ = true;
    telemetry::TelemetryService::Config telemetry_config_ = {};
    telemetry::TelemetryService telemetry_service_;

    // Name buffers for dynamically-named nodes
    char motorNodeNames_[kMaxMotors][32] = {};
    char servoNodeNames_[kMaxServos][32] = {};
};

}  // namespace chopper
