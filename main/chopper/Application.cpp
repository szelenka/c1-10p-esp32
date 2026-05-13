#include "chopper/Application.h"
#include "esp_log.h"
#include <cstdio>
#include <cstring>
#include <utility>

static const char* const TAG = "Application";

namespace chopper {

Application::Application(const core::Executor::Config& config) : executor_(config) {
    // Wire the disconnect handler for Bluetooth controllers
    controllerManager_.setDisconnectBehavior(disconnectHandler_.getBehavior());
}

bool Application::addMotor(uint8_t motor_id, hal::IMotorDriver* driver, const char* topic) {
    if ((driver == nullptr) || motorCount_ >= kMaxMotors) {
        return false;
    }
    motors_[motorCount_] = {motor_id, driver, topic};
    motorCount_++;
    return true;
}

bool Application::addServoController(hal::IServoController* controller, const char* topic) {
    if ((controller == nullptr) || servoCount_ >= kMaxServos) {
        return false;
    }
    servos_[servoCount_] = {controller, topic};
    servoCount_++;
    return true;
}

bool Application::addAudio(hal::IAudioDriver* driver, const char* topic) {
    if ((driver == nullptr) || (audioDriver_ != nullptr)) {
        return false;
    }
    audioDriver_ = driver;
    audioTopic_ = topic;
    return true;
}

bool Application::addNode(const core::NodePtr& node) {
    return executor_.addNode(node);
}

bool Application::init() {
    if (initialized_) {
        ESP_LOGW(TAG, "Already initialized");
        return false;
    }

    ESP_LOGI(TAG, "Initializing application...");

    // 1. Wire e-stop chain: Executor → SafetyManager
    executor_.setEmergencyStopCallback(&safety::SafetyManager::onExecutorEStop, &safetyManager_);

    // 2. Register e-stop chain step for driver shutdown
    safetyManager_.getEmergencyStopChain().registerStep(
        1, "DriverShutdown", [](const char*, uint8_t, void*) { hal::DriverManager::getInstance().shutdownAll(); },
        nullptr);

    // 3. Create motor bridge nodes
    for (uint8_t i = 0; i < motorCount_; i++) {
        snprintf(motorNodeNames_[i], sizeof(motorNodeNames_[i]), "motor_bridge_%d", motors_[i].motor_id);

        auto node =
            std::make_shared<nodes::MotorBridgeNode>(motorNodeNames_[i], motors_[i].topic, motors_[i].driver,
                                                     motors_[i].motor_id, &safetyManager_.getDegradationManager());

        if (!executor_.addNode(node)) {
            ESP_LOGE(TAG, "Failed to add motor bridge node %d", i);
            return false;
        }
        ESP_LOGI(TAG, "Added motor bridge: id=%d topic=%s", motors_[i].motor_id, motors_[i].topic);
    }

    // 4. Create servo bridge nodes
    for (uint8_t i = 0; i < servoCount_; i++) {
        snprintf(servoNodeNames_[i], sizeof(servoNodeNames_[i]), "servo_bridge_%d", i);

        auto node = std::make_shared<nodes::ServoBridgeNode>(
            servoNodeNames_[i], servos_[i].topic, servos_[i].controller, &safetyManager_.getDegradationManager());

        if (!executor_.addNode(node)) {
            ESP_LOGE(TAG, "Failed to add servo bridge node %d", i);
            return false;
        }
        ESP_LOGI(TAG, "Added servo bridge: topic=%s channels=%d", servos_[i].topic,
                 servos_[i].controller->getChannelCount());
    }

    // 5. Create audio bridge node
    if (audioDriver_ != nullptr) {
        auto node = std::make_shared<nodes::AudioBridgeNode>("audio_bridge", audioTopic_, audioDriver_,
                                                             &safetyManager_.getDegradationManager());

        if (!executor_.addNode(node)) {
            ESP_LOGE(TAG, "Failed to add audio bridge node");
            return false;
        }
        ESP_LOGI(TAG, "Added audio bridge: topic=%s", audioTopic_);
    }

    // 6. Add safety node
    auto safetyNode = std::make_shared<nodes::SafetyNode>(&safetyManager_);
    if (!executor_.addNode(safetyNode)) {
        ESP_LOGE(TAG, "Failed to add safety node");
        return false;
    }

    // 6b. Add telemetry node and bring up telemetry service.
    if (telemetry_enabled_) {
        telemetry_service_.begin(telemetry_config_);
        auto telemetryTapNode = std::make_shared<nodes::TelemetryIOTapNode>(&telemetry_service_);
        if (!executor_.addNode(telemetryTapNode)) {
            ESP_LOGE(TAG, "Failed to add telemetry I/O tap node");
            return false;
        }
        auto telemetryNode = std::make_shared<nodes::TelemetryNode>(&executor_, &safetyManager_, &telemetry_service_,
                                                                    telemetry_config_.node_update_hz);
        if (!executor_.addNode(telemetryNode)) {
            ESP_LOGE(TAG, "Failed to add telemetry node");
            return false;
        }
    }

    // 7. Initialize all nodes
    if (!executor_.initializeNodes()) {
        ESP_LOGE(TAG, "Node initialization failed");
        return false;
    }

    // 8. Initialize drivers via DriverManager
    hal::DriverManager::getInstance().initAll();
    applyStartupSafeState();

    initialized_ = true;
    ESP_LOGI(TAG, "Application initialized successfully");
    return true;
}

void Application::applyStartupSafeState() {
    for (uint8_t i = 0; i < motorCount_; i++) {
        if (motors_[i].driver != nullptr) {
            motors_[i].driver->stop();
        }
    }

    for (uint8_t i = 0; i < servoCount_; i++) {
        if (servos_[i].controller != nullptr) {
            servos_[i].controller->disableAll();
        }
    }
}

bool Application::start() {
    if (!initialized_) {
        ESP_LOGE(TAG, "Cannot start: not initialized");
        return false;
    }

    // Activate all nodes
    if (!executor_.activateNodes()) {
        ESP_LOGE(TAG, "Node activation failed");
        return false;
    }

    // Start the executor (creates FreeRTOS task)
    if (!executor_.start()) {
        ESP_LOGE(TAG, "Executor start failed");
        return false;
    }

    ESP_LOGI(TAG, "Application started");
    return true;
}

void Application::stop() {
    ESP_LOGI(TAG, "Stopping application...");

    executor_.stop();
    executor_.deactivateNodes();
    telemetry_service_.shutdown();
    hal::DriverManager::getInstance().shutdownAll();

    ESP_LOGI(TAG, "Application stopped");
}

void Application::emergencyStop(const char* reason) {
    executor_.emergencyStop(reason);
}

void Application::clearEmergencyStop(const char* reason) {
    ESP_LOGW(TAG, "Clear emergency stop requested: %s", reason ? reason : "(none)");
    safetyManager_.resetEmergencyStop();
    executor_.clearEmergencyStop(reason);
    safetyManager_.getDegradationManager().forceMode(safety::DegradationMode::FULL_OPERATION);
}

void Application::softStop(const char* reason) {
    ESP_LOGW(TAG, "Soft stop requested: %s", reason ? reason : "(none)");
    executor_.softStop(reason);
    safetyManager_.getDegradationManager().forceMode(safety::DegradationMode::SAFE_STOP);
}

void Application::clearSoftStop(const char* reason) {
    ESP_LOGI(TAG, "Clear soft stop requested: %s", reason ? reason : "(none)");
    executor_.clearSoftStop(reason);
    if (!safetyManager_.isEmergencyStopped()) {
        safetyManager_.getDegradationManager().forceMode(safety::DegradationMode::FULL_OPERATION);
    }
}

}  // namespace chopper
