#pragma once

#include "chopper/core/Executor.h"
#include "chopper/examples/ExampleNodes.h"
#include "chopper/adapters/Bluepad32ControllerSource.h"
#include "chopper/core/ControllerDecorator.h"
#include <memory>

namespace chopper {
namespace examples {

/**
 * @brief Example showing how to integrate the new node architecture
 * with the existing Bluepad32 controller system
 *
 * This demonstrates the migration path from the monolithic sketch.cpp
 * approach to the modular node-based architecture.
 */
class NodeArchitectureExample {
public:
    /**
     * @brief Constructor
     */
    NodeArchitectureExample();

    /**
     * @brief Destructor
     */
    ~NodeArchitectureExample();

    /**
     * @brief Initialize the node architecture
     * @return true if successful
     */
    bool initialize();

    /**
     * @brief Start the executor and begin processing
     * @return true if successful
     */
    bool start();

    /**
     * @brief Stop the executor
     * @return true if successful
     */
    bool stop();

    /**
     * @brief Set controller decorator from existing system
     * This bridges the old and new architectures
     * @param controller_decorator Existing ControllerDecorator instance
     */
    void setControllerDecorator(std::shared_ptr<ControllerDecorator> controller_decorator);

    /**
     * @brief Get executor statistics
     * @return Executor statistics
     */
    core::Executor::Statistics getStatistics() const;

    /**
     * @brief Check if system is running
     * @return true if running
     */
    bool isRunning() const;

private:
    /**
     * @brief Emergency stop callback
     * @param reason Reason for emergency stop
     */
    static void onEmergencyStop(const char* reason, void* context);

    // Core executor
    std::unique_ptr<core::Executor> executor_;

    // Controller system
    std::shared_ptr<adapters::Bluepad32ControllerSource> controller_source_;
    std::shared_ptr<ControllerInputNode> controller_input_node_;

    // Example processing nodes (commented out for now)
    // std::shared_ptr<DriveControlNode> drive_control_node_;
    // std::shared_ptr<MotorControlNode> motor_control_node_;
    // std::shared_ptr<SensorNode> sensor_node_;

    bool initialized_;
};

/**
 * @brief Factory function to create a pre-configured node architecture
 * This provides a simple way to set up the system with sensible defaults
 * @return Configured NodeArchitectureExample instance
 */
std::unique_ptr<NodeArchitectureExample> createDefaultNodeArchitecture();

}  // namespace examples
}  // namespace chopper