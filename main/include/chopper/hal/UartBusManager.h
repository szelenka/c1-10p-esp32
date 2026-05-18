#pragma once

#include "chopper/hal/IDriver.h"
#include "esp_log.h"
#include <cstdint>
#include <cstring>

namespace chopper::hal {

/// Configuration for a single UART port.
struct UartPortConfig {
    uint8_t portId;  ///< Logical port ID (0..kMaxPorts-1)
    int8_t rxPin;    ///< GPIO pin for RX (-1 if unused)
    int8_t txPin;    ///< GPIO pin for TX (-1 if unused)
    uint32_t baudRate;
    bool isSoftwareSerial;  ///< true = EspSoftwareSerial, false = HardwareSerial
    bool isHalfDuplex;      ///< true = TX-only or RX-only
    const char* ownerName;  ///< Driver name for diagnostics
};

/**
 * Manages UART port allocation and pin-conflict detection.
 *
 * All UART ports (hardware and software serial) are registered here at
 * init time. The manager detects pin conflicts before any serial port
 * is opened, preventing subtle runtime data corruption.
 *
 * This is not an IDriver itself — it is used by the DriverManager to
 * validate allocations before initializing UART-dependent drivers.
 */
class UartBusManager {
public:
    UartBusManager(const UartBusManager&) = delete;
    UartBusManager& operator=(const UartBusManager&) = delete;

    static constexpr uint8_t kMaxPorts = 6;
    static constexpr int8_t kPinUnused = -1;
    static constexpr uint8_t kNoOwner = 0xFF;
    static constexpr uint8_t kMaxGpioNum = 40;  ///< ESP32 GPIO range

    static UartBusManager& getInstance() {
        static UartBusManager instance;
        return instance;
    }

    /**
     * Register a UART port configuration.
     * @return true if registered successfully, false on pin conflict or full.
     */
    bool acquirePort(const UartPortConfig& config) {
        if (config.portId >= kMaxPorts) {
            ESP_LOGE(kTag, "Port ID %d exceeds max %d", config.portId, kMaxPorts);
            return false;
        }
        if (m_allocated[config.portId]) {
            ESP_LOGE(kTag, "Port %d already allocated to '%s'", config.portId, m_ports[config.portId].ownerName);
            return false;
        }

        // Check for pin conflicts
        if (config.txPin >= 0 && !claimPin(config.txPin, config.portId, config.ownerName)) {
            return false;
        }
        if (config.rxPin >= 0 && !claimPin(config.rxPin, config.portId, config.ownerName)) {
            // Rollback TX pin claim
            if (config.txPin >= 0) {
                releasePin(config.txPin);
            }
            return false;
        }

        m_ports[config.portId] = config;
        m_allocated[config.portId] = true;
        m_allocatedCount++;

        ESP_LOGI(kTag, "Port %d acquired by '%s' (TX=%d, RX=%d, baud=%lu, %s)", config.portId, config.ownerName,
                 config.txPin, config.rxPin, (unsigned long)config.baudRate, config.isSoftwareSerial ? "SW" : "HW");
        return true;
    }

    /// Release a port for reassignment.
    void releasePort(uint8_t portId) {
        if (portId >= kMaxPorts || !m_allocated[portId]) {
            return;
        }

        const UartPortConfig& cfg = m_ports[portId];
        if (cfg.txPin >= 0) {
            releasePin(cfg.txPin);
        }
        if (cfg.rxPin >= 0) {
            releasePin(cfg.rxPin);
        }

        ESP_LOGI(kTag, "Port %d released from '%s'", portId, cfg.ownerName);
        m_allocated[portId] = false;
        m_allocatedCount--;
    }

    /// Check for pin conflicts across all registered ports. Returns true if no conflicts.
    [[nodiscard]] bool validateAllocations() const {
        // Already validated on acquire, but can be called for double-check
        for (uint8_t pin = 0; pin < kMaxGpioNum; pin++) {
            if (m_pinOwner[pin] != kNoOwner && !m_allocated[m_pinOwner[pin]]) {
                ESP_LOGE(kTag, "Stale pin ownership: GPIO%d claimed by port %d (not allocated)", pin, m_pinOwner[pin]);
                return false;
            }
        }
        return true;
    }

    /// Get the config for a specific port. Returns nullptr if not allocated.
    [[nodiscard]] const UartPortConfig* getPortConfig(uint8_t portId) const {
        if (portId >= kMaxPorts || !m_allocated[portId]) {
            return nullptr;
        }
        return &m_ports[portId];
    }

    /// Check if a GPIO pin is already claimed.
    [[nodiscard]] bool isPinClaimed(int8_t pin) const {
        if (pin < 0 || pin >= kMaxGpioNum) {
            return false;
        }
        return m_pinOwner[pin] != kNoOwner;
    }

    /// Get the owner name for a claimed pin. Returns nullptr if unclaimed.
    [[nodiscard]] const char* getPinOwner(int8_t pin) const {
        if (pin < 0 || pin >= kMaxGpioNum) {
            return nullptr;
        }
        uint8_t portId = m_pinOwner[pin];
        if (portId == kNoOwner || !m_allocated[portId]) {
            return nullptr;
        }
        return m_ports[portId].ownerName;
    }

    /// Get the number of allocated ports.
    [[nodiscard]] uint8_t getAllocatedCount() const { return m_allocatedCount; }

    /// Print all port allocations for diagnostics.
    void printAllocations() const {
        ESP_LOGI(kTag, "=== UART Allocations (%d/%d) ===", m_allocatedCount, kMaxPorts);
        for (uint8_t i = 0; i < kMaxPorts; i++) {
            if (m_allocated[i]) {
                const UartPortConfig& cfg = m_ports[i];
                ESP_LOGI(kTag, "  [%d] %-16s  TX=%2d  RX=%2d  baud=%lu  %s %s", i, cfg.ownerName, cfg.txPin, cfg.rxPin,
                         (unsigned long)cfg.baudRate, cfg.isSoftwareSerial ? "SW" : "HW",
                         cfg.isHalfDuplex ? "half" : "full");
            }
        }
    }

private:
    static constexpr const char* kTag = "UartBusMgr";

    UartBusManager() {
        memset(m_allocated, 0, sizeof(m_allocated));
        memset(m_pinOwner, kNoOwner, sizeof(m_pinOwner));
        memset(m_ports, 0, sizeof(m_ports));
    }

    bool claimPin(int8_t pin, uint8_t portId, const char* ownerName) {
        if (pin < 0 || pin >= kMaxGpioNum) {
            return true;  // unused pin is fine
        }
        if (m_pinOwner[pin] != kNoOwner) {
            uint8_t conflictPort = m_pinOwner[pin];
            ESP_LOGE(kTag,
                     "Pin conflict: GPIO%d requested by '%s' (port %d) "
                     "but already claimed by '%s' (port %d)",
                     pin, ownerName, portId, m_ports[conflictPort].ownerName, conflictPort);
            return false;
        }
        m_pinOwner[pin] = portId;
        return true;
    }

    void releasePin(int8_t pin) {
        if (pin >= 0 && pin < kMaxGpioNum) {
            m_pinOwner[pin] = kNoOwner;
        }
    }

    UartPortConfig m_ports[kMaxPorts]{};
    bool m_allocated[kMaxPorts]{};
    uint8_t m_pinOwner[kMaxGpioNum]{};
    uint8_t m_allocatedCount = 0;
};

}  // namespace chopper::hal
