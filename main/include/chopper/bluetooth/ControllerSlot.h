#pragma once

#include <cstdint>
#include <cstring>

namespace chopper::bluetooth {

/// Controller roles matching existing ControllerRoles enum values.
/// LED values retained for backward compatibility with player LED indicators.
enum class ControllerRole : uint8_t {
    UNASSIGNED = 0,  ///< Connected but no role assigned
    DRIVE = 1,       ///< 0001 - player LED pattern
    DOME = 3,        ///< 0011
    ANIMATION = 7,   ///< 0111
    CAMERA = 15,     ///< 1111
    VAMBRACE = 16,   ///< Split-input wrist controller; not a player LED pattern
};

/// Number of first-available roles (excluding UNASSIGNED and special split roles).
static constexpr uint8_t kRoleCount = 4;

/// Ordered list of roles for iteration (priority order).
static constexpr ControllerRole kAllRoles[kRoleCount] = {
    ControllerRole::DRIVE,
    ControllerRole::DOME,
    ControllerRole::ANIMATION,
    ControllerRole::CAMERA,
};

/// Returns a human-readable string for a ControllerRole.
inline const char* roleToString(ControllerRole role) {
    switch (role) {
        case ControllerRole::UNASSIGNED:
            return "UNASSIGNED";
        case ControllerRole::DRIVE:
            return "DRIVE";
        case ControllerRole::DOME:
            return "DOME";
        case ControllerRole::ANIMATION:
            return "ANIMATION";
        case ControllerRole::CAMERA:
            return "CAMERA";
        case ControllerRole::VAMBRACE:
            return "VAMBRACE";
    }
    return "UNKNOWN";
}

/// Returns the player LED bitmask for a role.
inline uint8_t roleToLedMask(ControllerRole role) {
    switch (role) {
        case ControllerRole::DRIVE:
            return 1;
        case ControllerRole::DOME:
            return 3;
        case ControllerRole::ANIMATION:
            return 7;
        case ControllerRole::CAMERA:
            return 15;
        case ControllerRole::UNASSIGNED:
        case ControllerRole::VAMBRACE:
            return 0;
    }
    return 0;
}

/// MAC address as 6 raw bytes.
struct MacAddress {
    uint8_t addr[6]{};

    MacAddress() { memset(addr, 0, 6); }

    [[nodiscard]] bool isZero() const {
        for (unsigned char i : addr) {
            if (i != 0) {
                return false;
            }
        }
        return true;
    }

    bool operator==(const MacAddress& other) const { return memcmp(addr, other.addr, 6) == 0; }

    bool operator!=(const MacAddress& other) const { return !(*this == other); }

    /// Format as "XX:XX:XX:XX:XX:XX" into a caller-provided buffer (>= 18 bytes).
    void format(char* buf, size_t bufLen) const {
        if (bufLen < 18) {
            return;
        }
        snprintf(buf, bufLen, "%02X:%02X:%02X:%02X:%02X:%02X", addr[0], addr[1], addr[2], addr[3], addr[4], addr[5]);
    }
};

/**
 * Represents one of 4 Bluetooth controller connection slots.
 *
 * Each slot has an independent state machine:
 *   EMPTY -> IDENTIFYING -> ASSIGNING -> ACTIVE -> DISCONNECTING -> EMPTY
 *   EMPTY -> REJECTED -> EMPTY (if allowlist fails)
 */
struct ControllerSlot {
    /// Slot state machine states.
    enum class State : uint8_t {
        EMPTY,          ///< No controller connected
        IDENTIFYING,    ///< Reading properties after connect
        ASSIGNING,      ///< Waiting for role assignment
        ACTIVE,         ///< Producing input
        DISCONNECTING,  ///< Fallback behavior active after disconnect
        REJECTED,       ///< Allowlist check failed; pending disconnect
    };

    // -- Identity --
    uint8_t slot_index = 0;
    MacAddress mac;
    uint16_t controller_type = 0;  ///< Bluepad32 controller type enum
    uint16_t vendor_id = 0;
    uint16_t product_id = 0;

    // -- State --
    State state = State::EMPTY;
    ControllerRole role = ControllerRole::UNASSIGNED;
    ControllerRole previous_role = ControllerRole::UNASSIGNED;
    MacAddress previous_mac;

    // -- Timing (milliseconds since boot) --
    uint64_t connect_time_ms = 0;
    uint64_t last_input_time_ms = 0;
    uint64_t disconnect_time_ms = 0;

    // -- Data --
    uint8_t battery_level = 0;  ///< 0=unknown, 1=empty, 255=full

    // -- Reconnection --
    static constexpr uint64_t kReconnectRoleHoldMs = 30000;  ///< 30 seconds

    [[nodiscard]] bool isEmpty() const { return state == State::EMPTY; }
    [[nodiscard]] bool isActive() const { return state == State::ACTIVE; }

    /// Check if a previously-held role can be reclaimed on reconnect.
    [[nodiscard]] bool canReclaimPreviousRole(uint64_t now_ms) const {
        const bool mac_matches = previous_mac.isZero() || (mac == previous_mac);
        return previous_role != ControllerRole::UNASSIGNED && mac_matches && disconnect_time_ms > 0 &&
               (now_ms - disconnect_time_ms) < kReconnectRoleHoldMs;
    }

    /// Transition to EMPTY and preserve previous_role for reconnection.
    void beginDisconnect(uint64_t now_ms) {
        state = State::DISCONNECTING;
        previous_role = role;
        previous_mac = mac;
        disconnect_time_ms = now_ms;
    }

    /// Clear all slot data (final cleanup after disconnect).
    void clear() {
        state = State::EMPTY;
        role = ControllerRole::UNASSIGNED;
        // Preserve previous_role and disconnect_time for reconnection
        mac = MacAddress();
        controller_type = 0;
        vendor_id = 0;
        product_id = 0;
        battery_level = 0;
        connect_time_ms = 0;
        last_input_time_ms = 0;
    }

    /// Full reset including reconnection data.
    void fullReset() {
        clear();
        previous_role = ControllerRole::UNASSIGNED;
        previous_mac = MacAddress();
        disconnect_time_ms = 0;
    }

    /// Returns a human-readable state name.
    static const char* stateToString(State s) {
        switch (s) {
            case State::EMPTY:
                return "EMPTY";
            case State::IDENTIFYING:
                return "IDENTIFYING";
            case State::ASSIGNING:
                return "ASSIGNING";
            case State::ACTIVE:
                return "ACTIVE";
            case State::DISCONNECTING:
                return "DISCONNECTING";
            case State::REJECTED:
                return "REJECTED";
        }
        return "UNKNOWN";
    }
};

}  // namespace chopper::bluetooth
