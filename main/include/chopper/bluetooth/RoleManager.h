#pragma once

#include "chopper/bluetooth/ControllerSlot.h"
#include "chopper/chopper_limits.h"
#include "esp_log.h"
#include <cstring>

namespace chopper::bluetooth {

/**
 * Role assignment policy interface.
 *
 * Uses function pointer + void* context pattern (no std::function).
 */
struct RoleAssignmentPolicy {
    /// Policy name for diagnostics.
    const char* name;

    /**
     * Determine which role to assign to a newly connected controller.
     *
     * @param mac          MAC address of the controller.
     * @param ctlType      Bluepad32 controller type ID.
     * @param slots        Array of all 4 slots (for checking what roles are taken).
     * @param slotCount    Number of slots in the array.
     * @param context      Opaque context pointer for the policy implementation.
     * @return             The role to assign (UNASSIGNED if no role available).
     */
    ControllerRole (*assign)(const MacAddress& mac, uint16_t ctlType, const ControllerSlot* slots, uint8_t slotCount,
                             void* context);

    void* context;
};

/**
 * MAC-to-role mapping entry for MacBasedPolicy.
 */
struct MacRoleEntry {
    MacAddress mac;
    ControllerRole role = ControllerRole::UNASSIGNED;
};

/**
 * Mac-based role assignment (default, matches current SettingsBluetooth behavior).
 *
 * Looks up MAC address in a fixed-size table. If found and role is not
 * already taken, assigns that role. Otherwise assigns UNASSIGNED.
 */
class MacBasedPolicy {
public:
    static constexpr uint8_t kMaxEntries = limits::MAX_CONTROLLER_MAC_MAPPINGS;

    MacBasedPolicy() { memset(m_entries, 0, sizeof(m_entries)); }

    /// Add a MAC -> role mapping.
    bool addMapping(const MacAddress& mac, ControllerRole role) {
        if (m_entryCount >= kMaxEntries) {
            return false;
        }
        m_entries[m_entryCount].mac = mac;
        m_entries[m_entryCount].role = role;
        m_entryCount++;
        return true;
    }

    /// Get the policy descriptor for use with RoleManager.
    RoleAssignmentPolicy getPolicy() { return RoleAssignmentPolicy{"MacBased", &MacBasedPolicy::assignImpl, this}; }

private:
    static ControllerRole assignImpl(const MacAddress& mac, uint16_t /*ctlType*/, const ControllerSlot* slots,
                                     uint8_t slotCount, void* ctx) {
        auto* self = static_cast<MacBasedPolicy*>(ctx);

        // Look up MAC in the table
        for (uint8_t i = 0; i < self->m_entryCount; i++) {
            if (self->m_entries[i].mac == mac) {
                ControllerRole desired = self->m_entries[i].role;
                // Check if role is already taken by another slot
                for (uint8_t s = 0; s < slotCount; s++) {
                    if (slots[s].isActive() && slots[s].role == desired) {
                        return ControllerRole::UNASSIGNED;  // conflict
                    }
                }
                return desired;
            }
        }
        return ControllerRole::UNASSIGNED;
    }

    MacRoleEntry m_entries[kMaxEntries];
    uint8_t m_entryCount = 0;
};

/**
 * First-available policy: auto-assign roles in priority order.
 *
 * Checks if the MAC has a preferred role first. If not, assigns the
 * first unoccupied role in priority order: DRIVE > DOME > ANIMATION > CAMERA.
 */
class FirstAvailablePolicy {
public:
    static constexpr uint8_t kMaxEntries = limits::MAX_CONTROLLER_MAC_MAPPINGS;

    FirstAvailablePolicy() { memset(m_preferred, 0, sizeof(m_preferred)); }

    /// Set a preferred role for a specific MAC (optional).
    bool addPreference(const MacAddress& mac, ControllerRole role) {
        if (m_entryCount >= kMaxEntries) {
            return false;
        }
        m_preferred[m_entryCount].mac = mac;
        m_preferred[m_entryCount].role = role;
        m_entryCount++;
        return true;
    }

    RoleAssignmentPolicy getPolicy() {
        return RoleAssignmentPolicy{"FirstAvailable", &FirstAvailablePolicy::assignImpl, this};
    }

private:
    static bool isRoleTaken(ControllerRole role, const ControllerSlot* slots, uint8_t slotCount) {
        for (uint8_t s = 0; s < slotCount; s++) {
            if (slots[s].isActive() && slots[s].role == role) {
                return true;
            }
        }
        return false;
    }

    static ControllerRole assignImpl(const MacAddress& mac, uint16_t /*ctlType*/, const ControllerSlot* slots,
                                     uint8_t slotCount, void* ctx) {
        auto* self = static_cast<FirstAvailablePolicy*>(ctx);

        // Check if this MAC has a preferred role
        for (uint8_t i = 0; i < self->m_entryCount; i++) {
            if (self->m_preferred[i].mac == mac) {
                ControllerRole preferred = self->m_preferred[i].role;
                if (!isRoleTaken(preferred, slots, slotCount)) {
                    return preferred;
                }
                break;
            }
        }

        // Assign first available role in priority order
        for (auto kAllRole : kAllRoles) {
            if (!isRoleTaken(kAllRole, slots, slotCount)) {
                return kAllRole;
            }
        }

        return ControllerRole::UNASSIGNED;
    }

    MacRoleEntry m_preferred[kMaxEntries];
    uint8_t m_entryCount = 0;
};

/**
 * Manual policy: all controllers connect as UNASSIGNED.
 * Operator must explicitly assign roles via commands.
 */
class ManualPolicy {
public:
    RoleAssignmentPolicy getPolicy() { return RoleAssignmentPolicy{"Manual", &ManualPolicy::assignImpl, this}; }

private:
    static ControllerRole assignImpl(const MacAddress& /*mac*/, uint16_t /*ctlType*/, const ControllerSlot* /*slots*/,
                                     uint8_t /*slotCount*/, void* /*ctx*/) {
        return ControllerRole::UNASSIGNED;
    }
};

/**
 * Top-level role manager that orchestrates role assignment for all 4 slots.
 *
 * Uses a pluggable RoleAssignmentPolicy to determine which role a
 * newly connected controller receives. Also supports manual assignment,
 * role swapping, and role unassignment at runtime.
 */
class RoleManager {
public:
    static constexpr uint8_t kMaxSlots = limits::MAX_CONTROLLERS;

    RoleManager()

    {
        m_policy.name = "None";
        m_policy.assign = nullptr;
        m_policy.context = nullptr;
    }

    /// Set the slot array this manager operates on.
    void setSlots(ControllerSlot* slots, uint8_t count) {
        m_slots = slots;
        m_slotCount = count;
    }

    /// Set the active assignment policy.
    void setPolicy(const RoleAssignmentPolicy& policy) {
        m_policy = policy;
        ESP_LOGI(kTag, "Policy set to '%s'", policy.name);
    }

    /// Get the current policy name.
    [[nodiscard]] const char* getPolicyName() const { return m_policy.name; }

    /**
     * Called when a new controller enters the ASSIGNING state.
     * Uses the active policy to determine the role.
     * @return The assigned role.
     */
    ControllerRole onControllerAdded(uint8_t slotIndex, uint64_t now_ms) {
        if ((m_slots == nullptr) || slotIndex >= m_slotCount) {
            return ControllerRole::UNASSIGNED;
        }

        ControllerSlot& slot = m_slots[slotIndex];

        // Check if we can reclaim a previous role on reconnect
        if (slot.canReclaimPreviousRole(now_ms)) {
            ControllerRole prev = slot.previous_role;
            if (!isRoleTaken(prev, slotIndex)) {
                slot.role = prev;
                slot.state = ControllerSlot::State::ACTIVE;
                ESP_LOGD(kTag, "Slot %d reclaimed previous role %s", slotIndex, roleToString(prev));
                return prev;
            }
        }

        // Use policy to assign
        if (m_policy.assign != nullptr) {
            ControllerRole assigned =
                m_policy.assign(slot.mac, slot.controller_type, m_slots, m_slotCount, m_policy.context);
            slot.role = assigned;
        } else {
            slot.role = ControllerRole::UNASSIGNED;
        }

        slot.state = ControllerSlot::State::ACTIVE;
        ESP_LOGD(kTag, "Slot %d assigned role %s", slotIndex, roleToString(slot.role));
        return slot.role;
    }

    /**
     * Called when a controller disconnects.
     * The slot should already be in DISCONNECTING state.
     */
    void onControllerRemoved(uint8_t slotIndex) {
        if ((m_slots == nullptr) || slotIndex >= m_slotCount) {
            return;
        }
        ESP_LOGD(kTag, "Slot %d removed (was %s)", slotIndex, roleToString(m_slots[slotIndex].role));
    }

    /**
     * Manually assign a role to a slot.
     * @return true if assigned, false if role is occupied by another slot.
     */
    bool assignRole(uint8_t slotIndex, ControllerRole role) {
        if ((m_slots == nullptr) || slotIndex >= m_slotCount) {
            return false;
        }
        if (role != ControllerRole::UNASSIGNED && isRoleTaken(role, slotIndex)) {
            ESP_LOGW(kTag, "Cannot assign %s to slot %d: role occupied", roleToString(role), slotIndex);
            return false;
        }
        m_slots[slotIndex].role = role;
        ESP_LOGI(kTag, "Slot %d manually assigned role %s", slotIndex, roleToString(role));
        return true;
    }

    /**
     * Swap roles between two slots. Atomic operation.
     * @return true if both slots are active and swap succeeded.
     */
    bool swapRoles(uint8_t slotA, uint8_t slotB) {
        if ((m_slots == nullptr) || slotA >= m_slotCount || slotB >= m_slotCount) {
            return false;
        }
        if (!m_slots[slotA].isActive() || !m_slots[slotB].isActive()) {
            return false;
        }

        ControllerRole temp = m_slots[slotA].role;
        m_slots[slotA].role = m_slots[slotB].role;
        m_slots[slotB].role = temp;

        ESP_LOGI(kTag, "Swapped roles: slot %d=%s, slot %d=%s", slotA, roleToString(m_slots[slotA].role), slotB,
                 roleToString(m_slots[slotB].role));
        return true;
    }

    /// Remove role from a slot (sets to UNASSIGNED).
    void unassignRole(uint8_t slotIndex) {
        if ((m_slots == nullptr) || slotIndex >= m_slotCount) {
            return;
        }
        m_slots[slotIndex].role = ControllerRole::UNASSIGNED;
    }

    /// Get which slot holds a given role. Returns -1 if none.
    [[nodiscard]] int8_t getSlotForRole(ControllerRole role) const {
        if (m_slots == nullptr) {
            return -1;
        }
        for (uint8_t i = 0; i < m_slotCount; i++) {
            if (m_slots[i].isActive() && m_slots[i].role == role) {
                return static_cast<int8_t>(i);
            }
        }
        return -1;
    }

private:
    static constexpr const char* kTag = "RoleMgr";

    [[nodiscard]] bool isRoleTaken(ControllerRole role, uint8_t excludeSlot) const {
        for (uint8_t i = 0; i < m_slotCount; i++) {
            if (i == excludeSlot) {
                continue;
            }
            if (m_slots[i].isActive() && m_slots[i].role == role) {
                return true;
            }
        }
        return false;
    }

    ControllerSlot* m_slots = nullptr;
    uint8_t m_slotCount = 0;
    RoleAssignmentPolicy m_policy{};
};

}  // namespace chopper::bluetooth
