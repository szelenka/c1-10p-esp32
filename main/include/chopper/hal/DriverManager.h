#pragma once

#include "chopper/hal/IDriver.h"
#include "chopper/chopper_limits.h"
#include "esp_timer.h"
#include "esp_log.h"
#include <cstring>

namespace chopper::hal {

/**
 * Singleton manager that owns all IDriver instances and orchestrates
 * their lifecycle (init, update, shutdown) in priority order.
 *
 * Lower priority values are initialized first and updated first.
 * Shutdown proceeds in reverse priority order.
 *
 * The DriverManager tracks per-driver update timing and can flag
 * drivers that exceed their timing budget.
 */
class DriverManager {
public:
    static constexpr uint8_t kMaxDrivers = limits::MAX_DRIVERS;
    static constexpr uint64_t kTimingBudgetWarnUs = 2000;  ///< 2ms warning threshold per driver

    static DriverManager& getInstance();

    /**
     * Register a driver with a given priority.
     * Lower priority values are initialized first.
     * Must be called before initAll().
     * @return true if registered successfully, false if full or duplicate.
     */
    bool registerDriver(IDriver* driver, uint8_t priority = 128);

    /// Initialize all registered drivers in priority order (lowest first).
    void initAll();

    /// Call update() on all drivers with status kReady or kDegraded.
    void updateAll();

    /// Shutdown all drivers in reverse priority order.
    void shutdownAll();

    /// Reset a specific driver by name.
    bool resetDriver(const char* name) const;

    /// Get driver status by name. Returns kUninitialized if not found.
    DriverStatus getDriverStatus(const char* name) const;

    /// Find a driver by name. Returns nullptr if not found.
    IDriver* findDriver(const char* name) const;

    /// Get the number of registered drivers.
    [[nodiscard]] uint8_t getDriverCount() const { return m_driverCount; }

    /// Route a diagnostic command to a named driver.
    bool routeDiagnostic(const char* driverName, const char* command, char* response, size_t maxLen) const;

    /// Print diagnostic information for all drivers.
    void printDiagnostics() const;

    /// Per-driver timing statistics.
    struct DriverEntry {
        IDriver* driver;
        uint8_t priority;       ///< Lower = higher priority (initialized first)
        uint64_t lastUpdateUs;  ///< Duration of the most recent update() call
        uint64_t worstCaseUs;   ///< Worst observed update() duration
    };

    /// Get a driver entry by index (for diagnostics).
    [[nodiscard]] const DriverEntry* getEntry(uint8_t index) const;

private:
    DriverManager();

    /// Sort drivers by priority (insertion sort, called once after all registrations).
    void sortByPriority();

    DriverEntry m_drivers[kMaxDrivers] = {};
    uint8_t m_driverCount = 0;
    bool m_sorted = false;

public:
    DriverManager(const DriverManager&) = delete;
    DriverManager& operator=(const DriverManager&) = delete;
};

}  // namespace chopper::hal
