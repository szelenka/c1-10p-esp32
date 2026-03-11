#pragma once

#include "chopper/messages/CommonMessages.h"
#include <memory>
#include <string>

namespace chopper::core {

/**
 * @brief Abstract interface for controller input sources
 *
 * This interface allows the ControllerInputNode to work with different
 * controller backends (Bluepad32, DirectInput, keyboard simulation, etc.)
 * without tight coupling to any specific implementation.
 */
class IControllerSource {
public:
    /**
     * @brief Controller connection state
     */
    enum class ConnectionState {
        DISCONNECTED,  ///< No controller connected
        CONNECTING,    ///< Controller is connecting
        CONNECTED,     ///< Controller connected and ready
        ERROR          ///< Connection error
    };

    /**
     * @brief Controller capability flags
     */
    enum CapabilityFlags {
        HAS_ANALOG_STICKS = 1 << 0,   ///< Has analog stick support
        HAS_TRIGGERS = 1 << 1,        ///< Has analog triggers
        HAS_GYRO = 1 << 2,            ///< Has gyroscope
        HAS_ACCELEROMETER = 1 << 3,   ///< Has accelerometer
        HAS_RUMBLE = 1 << 4,          ///< Supports rumble/haptic feedback
        HAS_LED = 1 << 5,             ///< Has controllable LEDs
        HAS_BATTERY_STATUS = 1 << 6,  ///< Reports battery level
    };

    /**
     * @brief Virtual destructor
     */
    virtual ~IControllerSource() = default;

    /**
     * @brief Get current controller input data
     * @param output Reference to ControllerInput message to fill
     * @return true if data was successfully retrieved
     */
    virtual bool getControllerData(messages::ControllerInput& output) = 0;

    /**
     * @brief Get connection state
     * @return Current connection state
     */
    [[nodiscard]] virtual ConnectionState getConnectionState() const = 0;

    /**
     * @brief Check if controller has new data since last call
     * @return true if new data is available
     */
    [[nodiscard]] virtual bool hasNewData() const = 0;

    /**
     * @brief Get controller information
     * @return Human-readable controller information
     */
    [[nodiscard]] virtual std::string getControllerInfo() const = 0;  // NOLINT(heap) adapter interface, info-only

    /**
     * @brief Get controller capabilities
     * @return Bitfield of capability flags
     */
    [[nodiscard]] virtual uint32_t getCapabilities() const = 0;

    /**
     * @brief Get controller unique identifier
     * @return Unique identifier (e.g., MAC address, device path)
     */
    [[nodiscard]] virtual std::string getUniqueId() const = 0;  // NOLINT(heap) adapter interface, info-only

    /**
     * @brief Initialize the controller source
     * @return true if initialization successful
     */
    virtual bool initialize() = 0;

    /**
     * @brief Cleanup and shutdown the controller source
     */
    virtual void shutdown() = 0;

    /**
     * @brief Set controller output (rumble, LEDs, etc.)
     * @param red LED red component (0-255)
     * @param green LED green component (0-255)
     * @param blue LED blue component (0-255)
     * @param rumble_strength Rumble strength (0.0-1.0)
     * @return true if output was set successfully
     */
    virtual bool setControllerOutput(uint8_t red = 0, uint8_t green = 0, uint8_t blue = 0,
                                     float rumble_strength = 0.0f) {
        // Default implementation does nothing
        return false;
    }

    /**
     * @brief Check if controller is ready for input
     * @return true if ready (connected and has data)
     */
    [[nodiscard]] bool isReady() const { return getConnectionState() == ConnectionState::CONNECTED && hasNewData(); }
};

/**
 * @brief Smart pointer type for controller sources
 */
using ControllerSourcePtr = std::shared_ptr<IControllerSource>;

/**
 * @brief Factory function type for creating controller sources
 */
using ControllerSourceFactory = std::function<ControllerSourcePtr()>;  // NOLINT(heap) factory, init-time only

}  // namespace chopper::core
