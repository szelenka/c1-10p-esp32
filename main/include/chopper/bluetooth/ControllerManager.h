#pragma once

#include "chopper/bluetooth/ControllerSlot.h"
#include "chopper/bluetooth/RoleManager.h"
#include "chopper/bluetooth/ButtonMappingProfile.h"
#include "chopper/bluetooth/InputMixer.h"
#include "chopper/messages/CommonMessages.h"
#include "chopper/chopper_limits.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <cstring>

namespace chopper::bluetooth {

/**
 * Disconnect handler interface.
 *
 * Each role defines its own fallback behavior when a controller disconnects.
 * Uses function pointer + void* context (no std::function).
 */
struct DisconnectBehavior {
    /// Generate fallback input for a disconnected controller.
    /// @param role     The role that disconnected.
    /// @param lastInput The last input received before disconnect.
    /// @param output   Output fallback input message.
    void (*getFallback)(ControllerRole role, const messages::ControllerInput& lastInput,
                        messages::ControllerInput& output, void* context);

    /// How long to publish the fallback before going to zero (ms).
    uint32_t (*getFallbackDurationMs)(ControllerRole role, void* context);

    void* context;
};

/**
 * Default disconnect handler per the design doc:
 *   DRIVE     -> immediate zero (safety stop)
 *   DOME      -> hold last position
 *   ANIMATION -> copy last state (animation node handles timeout)
 *   CAMERA    -> hold last position
 */
class DefaultDisconnectHandler {
public:
    DisconnectBehavior getBehavior() {
        return DisconnectBehavior{&DefaultDisconnectHandler::fallbackImpl, &DefaultDisconnectHandler::durationImpl,
                                  this};
    }

private:
    static void fallbackImpl(ControllerRole role, const messages::ControllerInput& lastInput,
                             messages::ControllerInput& output, void* /*ctx*/) {
        // Start from zero
        output = messages::ControllerInput{};
        output.is_connected = false;

        switch (role) {
            case ControllerRole::DRIVE:
                // All zeros — immediate stop
                break;
            case ControllerRole::DOME:
            case ControllerRole::CAMERA:
                // Hold last dome/camera position
                output.axis_rx = lastInput.axis_rx;
                output.axis_ry = lastInput.axis_ry;
                output.axis_rx_normalized = lastInput.axis_rx_normalized;
                output.axis_ry_normalized = lastInput.axis_ry_normalized;
                break;
            case ControllerRole::ANIMATION:
                // Copy last state; animation node handles its own timeout
                output = lastInput;
                output.is_connected = false;
                break;
            case ControllerRole::UNASSIGNED:
                break;
        }
    }

    static uint32_t durationImpl(ControllerRole role, void* /*ctx*/) {
        switch (role) {
            case ControllerRole::DRIVE:
                return 0;
            case ControllerRole::DOME:
                return 0;
            case ControllerRole::ANIMATION:
                return 2000;
            case ControllerRole::CAMERA:
                return 0;
            case ControllerRole::UNASSIGNED:
                return 0;
        }
        return 0;
    }
};

/**
 * Top-level controller manager for up to 4 simultaneous Bluetooth controllers.
 *
 * Owns the slot array, role manager, disconnect handler, and input mixer.
 * Provides the main connect/disconnect/update interface for the system.
 */
class ControllerManager {
public:
    static constexpr uint8_t kMaxSlots = limits::MAX_CONTROLLERS;
    static constexpr uint64_t kDefaultTimeoutMs = 200;  ///< Disconnect detection watchdog
    using ConnectCallback = void (*)(uint8_t slot_index, ControllerRole role, void* context);
    using UnexpectedDisconnectCallback = void (*)(uint8_t slot_index, ControllerRole role, uint64_t stale_ms,
                                                  void* context);

    ControllerManager()

    {
        for (uint8_t i = 0; i < kMaxSlots; i++) {
            m_slots[i].slot_index = i;
            m_slots[i].fullReset();
        }
        m_roleManager.setSlots(m_slots, kMaxSlots);
    }

    // -- Slot accessors --

    [[nodiscard]] const ControllerSlot& getSlot(uint8_t index) const { return m_slots[index < kMaxSlots ? index : 0]; }

    ControllerSlot& getSlot(uint8_t index) { return m_slots[index < kMaxSlots ? index : 0]; }

    /// Find a slot by MAC address. Returns -1 if not found.
    [[nodiscard]] int8_t findSlotByMac(const MacAddress& mac) const {
        for (uint8_t i = 0; i < kMaxSlots; i++) {
            if (m_slots[i].mac == mac && !m_slots[i].isEmpty()) {
                return static_cast<int8_t>(i);
            }
        }
        return -1;
    }

    /// Find a slot by role. Returns -1 if no slot has that role.
    [[nodiscard]] int8_t findSlotByRole(ControllerRole role) const { return m_roleManager.getSlotForRole(role); }

    /// Find first empty slot. Returns -1 if all occupied.
    [[nodiscard]] int8_t findEmptySlot() const {
        for (uint8_t i = 0; i < kMaxSlots; i++) {
            if (m_slots[i].isEmpty()) {
                return static_cast<int8_t>(i);
            }
        }
        return -1;
    }

    /// Count of active (producing input) controllers.
    [[nodiscard]] uint8_t getActiveCount() const {
        uint8_t count = 0;
        for (const auto& m_slot : m_slots) {
            if (m_slot.isActive()) {
                count++;
            }
        }
        return count;
    }

    // -- Connection management --

    /**
     * Handle a new controller connection.
     * Called from the Bluepad32 onConnect callback (or test code).
     *
     * @param mac           MAC address of the controller.
     * @param ctlType       Bluepad32 controller type.
     * @param vendorId      USB vendor ID.
     * @param productId     USB product ID.
     * @param now_ms        Current time in milliseconds.
     * @return Slot index assigned, or -1 if rejected (full or not allowed).
     */
    int8_t onConnect(const MacAddress& mac, uint16_t ctlType, uint16_t vendorId, uint16_t productId, uint64_t now_ms) {
        int8_t idx = findEmptySlot();
        if (idx < 0) {
            ESP_LOGW(kTag, "All slots full, rejecting controller");
            return -1;
        }

        ControllerSlot& slot = m_slots[idx];
        slot.mac = mac;
        slot.controller_type = ctlType;
        slot.vendor_id = vendorId;
        slot.product_id = productId;
        slot.connect_time_ms = now_ms;
        // Use fresh wall-clock time so the watchdog window starts from the
        // actual connection moment, not the (potentially stale) loop-start
        // timestamp — BT handshake can block for >100 ms.
        slot.last_input_time_ms = static_cast<uint64_t>(esp_timer_get_time() / 1000ULL);
        slot.state = ControllerSlot::State::IDENTIFYING;

        // Move through IDENTIFYING -> ASSIGNING -> ACTIVE
        slot.state = ControllerSlot::State::ASSIGNING;
        ControllerRole assigned = m_roleManager.onControllerAdded(idx, now_ms);

        char macStr[18];
        mac.format(macStr, sizeof(macStr));
        ESP_LOGI(kTag, "Controller connected: slot=%d mac=%s type=%d role=%s", idx, macStr, ctlType,
                 roleToString(assigned));

        if (m_connectCallback != nullptr) {
            m_connectCallback(static_cast<uint8_t>(idx), assigned, m_connectContext);
        }

        return idx;
    }

    /**
     * Handle a controller disconnection.
     *
     * @param slotIndex     The slot that disconnected.
     * @param now_ms        Current time in milliseconds.
     */
    void onDisconnect(uint8_t slotIndex, uint64_t now_ms) {
        if (slotIndex >= kMaxSlots) {
            return;
        }
        ControllerSlot& slot = m_slots[slotIndex];
        if (slot.isEmpty()) {
            return;
        }

        ESP_LOGD(kTag, "Controller disconnected: slot=%d role=%s", slotIndex, roleToString(slot.role));

        slot.beginDisconnect(now_ms);
        m_roleManager.onControllerRemoved(slotIndex);

        // Apply disconnect fallback
        if (m_disconnectBehavior.getFallback != nullptr) {
            m_disconnectBehavior.getFallback(slot.role, m_lastInputs[slotIndex], m_fallbackInputs[slotIndex],
                                             m_disconnectBehavior.context);
            m_fallbackActive[slotIndex] = true;
            m_fallbackStartMs[slotIndex] = now_ms;

            uint32_t dur = (m_disconnectBehavior.getFallbackDurationMs != nullptr)
                               ? m_disconnectBehavior.getFallbackDurationMs(slot.role, m_disconnectBehavior.context)
                               : 0;
            m_fallbackDurationMs[slotIndex] = dur;
        }

        // If no fallback duration, clear immediately
        if (!m_fallbackActive[slotIndex] || m_fallbackDurationMs[slotIndex] == 0) {
            finishDisconnect(slotIndex);
        }
    }

    /**
     * Called every update cycle to process fallback timeouts and
     * watchdog-based disconnect detection.
     *
     * @param now_ms  Current time in milliseconds.
     */
    void update(uint64_t now_ms) {
        for (uint8_t i = 0; i < kMaxSlots; i++) {
            // Process active fallbacks
            if (m_fallbackActive[i]) {
                const uint64_t elapsed_ms = (now_ms >= m_fallbackStartMs[i]) ? (now_ms - m_fallbackStartMs[i]) : 0;
                if (elapsed_ms >= m_fallbackDurationMs[i]) {
                    finishDisconnect(i);
                }
            }

            // Watchdog: detect stale controllers
            if (m_slots[i].isActive() && m_timeoutMs > 0) {
                if (now_ms < m_slots[i].last_input_time_ms) {
                    // Ignore transient clock-domain ordering races.
                    continue;
                }
                const uint64_t stale_ms = now_ms - m_slots[i].last_input_time_ms;
                if (stale_ms > m_timeoutMs) {
                    ESP_LOGW(kTag, "Slot %d watchdog timeout (%llu ms)", i, (unsigned long long)stale_ms);
                    if (m_unexpectedDisconnectCallback != nullptr) {
                        m_unexpectedDisconnectCallback(i, m_slots[i].role, stale_ms, m_unexpectedDisconnectContext);
                    }
                    onDisconnect(i, now_ms);
                }
            }
        }
    }

    /// Keep the watchdog alive for a connected slot without updating input data.
    /// Call this every cycle a controller is seen as connected by the BT stack,
    /// even if no fresh gamepad report has arrived yet.
    void feedSlotWatchdog(uint8_t slotIndex, uint64_t now_ms) {
        if (slotIndex >= kMaxSlots) {
            return;
        }
        m_slots[slotIndex].last_input_time_ms = now_ms;
    }

    /// Record that input was received from a slot (updates watchdog timer).
    void recordInput(uint8_t slotIndex, uint64_t now_ms, const messages::ControllerInput& input) {
        if (slotIndex >= kMaxSlots) {
            return;
        }
        m_slots[slotIndex].last_input_time_ms = now_ms;
        m_lastInputs[slotIndex] = input;
    }

    /// Get the fallback input for a disconnecting slot (if any).
    bool getFallbackInput(uint8_t slotIndex, messages::ControllerInput& output) const {
        if (slotIndex >= kMaxSlots || !m_fallbackActive[slotIndex]) {
            return false;
        }
        output = m_fallbackInputs[slotIndex];
        return true;
    }

    // -- Role management delegation --

    RoleManager& getRoleManager() { return m_roleManager; }
    [[nodiscard]] const RoleManager& getRoleManager() const { return m_roleManager; }

    /// Set the disconnect behavior handler.
    void setDisconnectBehavior(const DisconnectBehavior& behavior) { m_disconnectBehavior = behavior; }

    /// Callback for watchdog-triggered (unexpected) disconnects.
    void setUnexpectedDisconnectCallback(UnexpectedDisconnectCallback cb, void* context = nullptr) {
        m_unexpectedDisconnectCallback = cb;
        m_unexpectedDisconnectContext = context;
    }

    /// Callback for controller connections.
    void setConnectCallback(ConnectCallback cb, void* context = nullptr) {
        m_connectCallback = cb;
        m_connectContext = context;
    }

    /// Get the input mixer for configuring mixing rules.
    InputMixer& getInputMixer() { return m_inputMixer; }

    /// Set watchdog timeout (ms). 0 to disable.
    void setTimeoutMs(uint64_t ms) { m_timeoutMs = ms; }
    [[nodiscard]] uint64_t getTimeoutMs() const { return m_timeoutMs; }

    /// Get the profile for a slot's controller type.
    [[nodiscard]] const ButtonMappingProfile* getProfileForSlot(uint8_t slotIndex) const {
        if (slotIndex >= kMaxSlots) {
            return nullptr;
        }
        return findProfile(m_slots[slotIndex].controller_type);
    }

    /// Emergency stop: zero all outputs, set all fallbacks inactive.
    void emergencyStop() {
        for (uint8_t i = 0; i < kMaxSlots; i++) {
            m_lastInputs[i] = messages::ControllerInput{};
            m_fallbackInputs[i] = messages::ControllerInput{};
            m_fallbackActive[i] = false;
        }
        ESP_LOGW(kTag, "Emergency stop: all controller inputs zeroed");
    }

private:
    static constexpr const char* kTag = "CtlMgr";

    void finishDisconnect(uint8_t slotIndex) {
        m_fallbackActive[slotIndex] = false;
        m_slots[slotIndex].clear();
        ESP_LOGD(kTag, "Slot %d disconnect complete", slotIndex);
    }

    ControllerSlot m_slots[kMaxSlots];
    RoleManager m_roleManager;
    InputMixer m_inputMixer;
    DisconnectBehavior m_disconnectBehavior = {};
    uint64_t m_timeoutMs = kDefaultTimeoutMs;

    // Per-slot state for disconnect handling
    messages::ControllerInput m_lastInputs[kMaxSlots] = {};
    messages::ControllerInput m_fallbackInputs[kMaxSlots] = {};
    bool m_fallbackActive[kMaxSlots] = {};
    uint64_t m_fallbackStartMs[kMaxSlots] = {};
    uint32_t m_fallbackDurationMs[kMaxSlots] = {};
    ConnectCallback m_connectCallback = nullptr;
    void* m_connectContext = nullptr;
    UnexpectedDisconnectCallback m_unexpectedDisconnectCallback = nullptr;
    void* m_unexpectedDisconnectContext = nullptr;
};

}  // namespace chopper::bluetooth
