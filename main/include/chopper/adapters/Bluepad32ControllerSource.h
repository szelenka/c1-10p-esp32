#pragma once

#include "chopper/core/IControllerSource.h"
#include "chopper/core/ControllerDecorator.h"
#include <memory>

namespace chopper {
namespace adapters {

/**
 * @brief Bluepad32 implementation of IControllerSource
 *
 * This adapter wraps the existing ControllerDecorator to provide
 * a clean interface that works with our new node architecture.
 */
class Bluepad32ControllerSource : public core::IControllerSource {
public:
    /**
     * @brief Constructor
     */
    Bluepad32ControllerSource();

    /**
     * @brief Constructor with existing controller decorator
     * @param controller_decorator Existing ControllerDecorator instance
     */
    explicit Bluepad32ControllerSource(std::shared_ptr<ControllerDecorator> controller_decorator);

    /**
     * @brief Destructor
     */
    ~Bluepad32ControllerSource() override = default;

    // IControllerSource implementation
    bool getControllerData(messages::ControllerInput& output) override;
    ConnectionState getConnectionState() const override;
    bool hasNewData() const override;
    std::string getControllerInfo() const override;
    uint32_t getCapabilities() const override;
    std::string getUniqueId() const override;
    bool initialize() override;
    void shutdown() override;
    bool setControllerOutput(uint8_t red, uint8_t green, uint8_t blue, float rumble_strength) override;

    /**
     * @brief Set the controller decorator to use
     * @param controller_decorator ControllerDecorator instance
     */
    void setControllerDecorator(std::shared_ptr<ControllerDecorator> controller_decorator);

    /**
     * @brief Get the underlying controller decorator
     * @return Pointer to ControllerDecorator (may be null)
     */
    std::shared_ptr<ControllerDecorator> getControllerDecorator() const { return controller_; }

private:
    /**
     * @brief Convert ControllerDecorator data to ControllerInput efficiently
     * @param output Output ControllerInput message
     */
    void convertControllerData(messages::ControllerInput& output);

    /**
     * @brief Safe conversion using public ControllerDecorator interface
     * @param output Output ControllerInput message
     */
    void convertControllerDataSafe(messages::ControllerInput& output);

    /**
     * @brief Get controller capabilities based on Bluepad32 features
     * @return Capability flags
     */
    uint32_t determineCapabilities() const;

    std::shared_ptr<ControllerDecorator> controller_;
    bool initialized_;
    mutable ConnectionState cached_state_;
    mutable uint64_t last_state_check_time_;
    static constexpr uint64_t STATE_CACHE_TIME_US = 10000; // 10ms cache
};

} // namespace adapters
} // namespace chopper