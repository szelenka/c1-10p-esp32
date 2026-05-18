#pragma once

#include <cstdint>
#include <cstddef>
#include <cstring>

namespace chopper::hal {

/// Driver lifecycle states.
enum class DriverStatus : uint8_t {
    kUninitialized = 0,  ///< init() not yet called
    kReady,              ///< Operating normally
    kDegraded,           ///< Operating with warnings (e.g., high latency)
    kError,              ///< Non-recoverable error; needs reset()
    kDisabled,           ///< Explicitly disabled via shutdown()
};

/// Error information for driver diagnostics.
struct ErrorInfo {
    uint16_t code = 0;       ///< Driver-specific error code (0 = no error)
    uint64_t timestamp = 0;  ///< When the error occurred (ms since boot)
    char message[64]{};      ///< Human-readable description

    ErrorInfo() { message[0] = '\0'; }

    void set(uint16_t c, uint64_t ts, const char* msg) {
        code = c;
        timestamp = ts;
        if (msg != nullptr) {
            strncpy(message, msg, sizeof(message) - 1);
            message[sizeof(message) - 1] = '\0';
        } else {
            message[0] = '\0';
        }
    }

    void clear() {
        code = 0;
        timestamp = 0;
        message[0] = '\0';
    }
};

/**
 * Base interface for all hardware drivers.
 *
 * Every peripheral in the system implements this interface, providing
 * a uniform lifecycle: UNINITIALIZED -> READY -> DEGRADED -> ERROR -> DISABLED.
 */
class IDriver {
public:
    virtual ~IDriver() = default;

    /// One-time initialization (configure pins, baud, etc.)
    virtual DriverStatus init() = 0;

    /// Called each executor cycle (send/receive data). Must be real-time safe.
    virtual void update() = 0;

    /// Current operational state.
    [[nodiscard]] virtual DriverStatus getStatus() const = 0;

    /// Last error code + human-readable message.
    [[nodiscard]] virtual ErrorInfo getErrorState() const = 0;

    /// Driver identifier for diagnostics.
    [[nodiscard]] virtual const char* getName() const = 0;

    /// Re-initialize without full system restart.
    virtual DriverStatus reset() = 0;

    /// Graceful power-down / disable outputs.
    virtual void shutdown() = 0;

    /// Diagnostic command interface. Returns true if the command was handled.
    virtual bool handleDiagnostic(const char* command, char* response, size_t maxLen) {
        (void)command;
        (void)response;
        (void)maxLen;
        return false;
    }
};

/// Returns a human-readable string for a DriverStatus value.
inline const char* driverStatusToString(DriverStatus status) {
    switch (status) {
        case DriverStatus::kUninitialized:
            return "UNINITIALIZED";
        case DriverStatus::kReady:
            return "READY";
        case DriverStatus::kDegraded:
            return "DEGRADED";
        case DriverStatus::kError:
            return "ERROR";
        case DriverStatus::kDisabled:
            return "DISABLED";
    }
    return "UNKNOWN";
}

}  // namespace chopper::hal
