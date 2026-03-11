#include "chopper/hal/DriverManager.h"
#include <cstring>

static const char* const TAG = "DriverManager";

namespace chopper::hal {

DriverManager& DriverManager::getInstance() {
    static DriverManager instance;
    return instance;
}

DriverManager::DriverManager() = default;

bool DriverManager::registerDriver(IDriver* driver, uint8_t priority) {
    if (driver == nullptr) {
        ESP_LOGE(TAG, "Cannot register null driver");
        return false;
    }
    if (m_driverCount >= kMaxDrivers) {
        ESP_LOGE(TAG, "Cannot register '%s': max drivers (%d) reached", driver->getName(), kMaxDrivers);
        return false;
    }

    // Check for duplicate registration
    for (uint8_t i = 0; i < m_driverCount; i++) {
        if (m_drivers[i].driver == driver) {
            ESP_LOGW(TAG, "Driver '%s' already registered", driver->getName());
            return false;
        }
    }

    DriverEntry& entry = m_drivers[m_driverCount];
    entry.driver = driver;
    entry.priority = priority;
    entry.lastUpdateUs = 0;
    entry.worstCaseUs = 0;
    m_driverCount++;
    m_sorted = false;

    ESP_LOGI(TAG, "Registered driver '%s' with priority %d (%d/%d)", driver->getName(), priority, m_driverCount,
             kMaxDrivers);
    return true;
}

void DriverManager::sortByPriority() {
    // Insertion sort — stable, efficient for small N (max 12 drivers)
    for (uint8_t i = 1; i < m_driverCount; i++) {
        DriverEntry temp = m_drivers[i];
        int j = i - 1;
        while (j >= 0 && m_drivers[j].priority > temp.priority) {
            m_drivers[j + 1] = m_drivers[j];
            j--;
        }
        m_drivers[j + 1] = temp;
    }
    m_sorted = true;
}

void DriverManager::initAll() {
    if (!m_sorted) {
        sortByPriority();
    }

    ESP_LOGI(TAG, "Initializing %d drivers...", m_driverCount);

    for (uint8_t i = 0; i < m_driverCount; i++) {
        IDriver* drv = m_drivers[i].driver;
        ESP_LOGI(TAG, "  [%d] init '%s' (priority %d)", i, drv->getName(), m_drivers[i].priority);

        DriverStatus status = drv->init();
        if (status != DriverStatus::kReady) {
            ESP_LOGE(TAG, "  '%s' init failed: %s", drv->getName(), driverStatusToString(status));
        }
    }

    ESP_LOGI(TAG, "Driver initialization complete");
}

void DriverManager::updateAll() {
    for (uint8_t i = 0; i < m_driverCount; i++) {
        DriverStatus status = m_drivers[i].driver->getStatus();
        if (status == DriverStatus::kReady || status == DriverStatus::kDegraded) {
            uint64_t start = esp_timer_get_time();
            m_drivers[i].driver->update();
            auto elapsed = static_cast<uint64_t>(esp_timer_get_time() - start);

            m_drivers[i].lastUpdateUs = elapsed;
            if (elapsed > m_drivers[i].worstCaseUs) {
                m_drivers[i].worstCaseUs = elapsed;
            }
        }
    }
}

void DriverManager::shutdownAll() {
    ESP_LOGI(TAG, "Shutting down %d drivers (reverse priority)...", m_driverCount);

    // Shutdown in reverse priority order
    for (int i = static_cast<int>(m_driverCount) - 1; i >= 0; i--) {
        IDriver* drv = m_drivers[i].driver;
        DriverStatus status = drv->getStatus();
        if (status != DriverStatus::kDisabled && status != DriverStatus::kUninitialized) {
            ESP_LOGI(TAG, "  shutdown '%s'", drv->getName());
            drv->shutdown();
        }
    }
}

bool DriverManager::resetDriver(const char* name) const {
    IDriver* drv = findDriver(name);
    if (drv == nullptr) {
        ESP_LOGW(TAG, "resetDriver: '%s' not found", name);
        return false;
    }

    ESP_LOGI(TAG, "Resetting driver '%s'", name);
    DriverStatus status = drv->reset();
    ESP_LOGI(TAG, "  '%s' reset result: %s", name, driverStatusToString(status));
    return (status == DriverStatus::kReady);
}

DriverStatus DriverManager::getDriverStatus(const char* name) const {
    const IDriver* drv = findDriver(name);
    if (drv == nullptr) {
        return DriverStatus::kUninitialized;
    }
    return drv->getStatus();
}

IDriver* DriverManager::findDriver(const char* name) const {
    for (uint8_t i = 0; i < m_driverCount; i++) {
        if (strcmp(m_drivers[i].driver->getName(), name) == 0) {
            return m_drivers[i].driver;
        }
    }
    return nullptr;
}

bool DriverManager::routeDiagnostic(const char* driverName, const char* command, char* response, size_t maxLen) const {
    IDriver* drv = findDriver(driverName);
    if (drv == nullptr) {
        if ((response != nullptr) && maxLen > 0) {
            snprintf(response, maxLen, "Driver '%s' not found", driverName);
        }
        return false;
    }
    return drv->handleDiagnostic(command, response, maxLen);
}

void DriverManager::printDiagnostics() const {
    ESP_LOGI(TAG, "=== Driver Diagnostics (%d drivers) ===", m_driverCount);
    for (uint8_t i = 0; i < m_driverCount; i++) {
        const DriverEntry& entry = m_drivers[i];
        ESP_LOGI(TAG, "  [%d] %-20s  status=%-14s  pri=%3d  last=%lluus  worst=%lluus", i, entry.driver->getName(),
                 driverStatusToString(entry.driver->getStatus()), entry.priority,
                 (unsigned long long)entry.lastUpdateUs, (unsigned long long)entry.worstCaseUs);

        ErrorInfo err = entry.driver->getErrorState();
        if (err.code != 0) {
            ESP_LOGW(TAG, "         error=%d: %s", err.code, err.message);
        }
    }
}

const DriverManager::DriverEntry* DriverManager::getEntry(uint8_t index) const {
    if (index >= m_driverCount) {
        return nullptr;
    }
    return &m_drivers[index];
}

}  // namespace chopper::hal
