#pragma once

#include "chopper/bluetooth/ControllerSlot.h"
#include "chopper/messages/CommonMessages.h"
#include <cstdint>

namespace chopper {
namespace bluetooth {

/**
 * Merges input from multiple controllers that affect the same subsystem.
 *
 * For example, dome spin requires R2 from both the Drive and Dome controllers.
 * The InputMixer defines rules for how overlapping inputs combine.
 */
class InputMixer {
public:
    enum class MixMode : uint8_t {
        PRIORITY,       ///< Higher-priority controller wins (role_a has priority)
        ADDITIVE,       ///< Sum inputs (clamped to [-1,1])
        AVERAGE,        ///< Average of both inputs
        MAX_MAGNITUDE,  ///< Use whichever input has larger absolute value
    };

    /// A mixing rule between two controller roles.
    struct MixRule {
        ControllerRole role_a;      ///< First role (higher priority in PRIORITY mode)
        ControllerRole role_b;      ///< Second role
        MixMode mode;
        uint32_t axis_mask;         ///< Bitmask of axes to apply mixing
        uint32_t button_mask;       ///< Bitmask of buttons to OR together
    };

    /// Axis mask bits for selecting which axes to mix.
    static constexpr uint32_t kAxisX   = 1 << 0;
    static constexpr uint32_t kAxisY   = 1 << 1;
    static constexpr uint32_t kAxisRX  = 1 << 2;
    static constexpr uint32_t kAxisRY  = 1 << 3;
    static constexpr uint32_t kAllAxes = kAxisX | kAxisY | kAxisRX | kAxisRY;

    static constexpr uint8_t kMaxRules = 8;

    InputMixer() : m_ruleCount(0) {}

    /// Register a mixing rule. Returns false if full.
    bool addRule(const MixRule& rule) {
        if (m_ruleCount >= kMaxRules) return false;
        m_rules[m_ruleCount++] = rule;
        return true;
    }

    /// Get the number of registered rules.
    uint8_t getRuleCount() const { return m_ruleCount; }

    /// Get a rule by index. Returns nullptr if out of range.
    const MixRule* getRule(uint8_t index) const {
        if (index >= m_ruleCount) return nullptr;
        return &m_rules[index];
    }

    /**
     * Apply a mixing rule to combine two ControllerInput messages.
     * Only the axes/buttons specified in the rule are mixed;
     * all other fields are taken from input_a.
     */
    static messages::ControllerInput mix(
        const messages::ControllerInput& input_a,
        const messages::ControllerInput& input_b,
        const MixRule& rule)
    {
        messages::ControllerInput result = input_a;

        // Mix normalized axes
        if (rule.axis_mask & kAxisX) {
            result.axis_x_normalized = mixFloat(
                input_a.axis_x_normalized, input_b.axis_x_normalized, rule.mode);
        }
        if (rule.axis_mask & kAxisY) {
            result.axis_y_normalized = mixFloat(
                input_a.axis_y_normalized, input_b.axis_y_normalized, rule.mode);
        }
        if (rule.axis_mask & kAxisRX) {
            result.axis_rx_normalized = mixFloat(
                input_a.axis_rx_normalized, input_b.axis_rx_normalized, rule.mode);
        }
        if (rule.axis_mask & kAxisRY) {
            result.axis_ry_normalized = mixFloat(
                input_a.axis_ry_normalized, input_b.axis_ry_normalized, rule.mode);
        }

        // Mix buttons with OR logic for the specified button mask
        if (rule.button_mask != 0) {
            uint16_t masked_a = input_a.buttons & static_cast<uint16_t>(rule.button_mask);
            uint16_t masked_b = input_b.buttons & static_cast<uint16_t>(rule.button_mask);
            uint16_t unmasked = result.buttons & ~static_cast<uint16_t>(rule.button_mask);
            result.buttons = unmasked | masked_a | masked_b;
        }

        return result;
    }

private:
    static float mixFloat(float a, float b, MixMode mode) {
        switch (mode) {
            case MixMode::PRIORITY:
                // a has priority; use a unless it's near zero, then use b
                return (a > 0.01f || a < -0.01f) ? a : b;

            case MixMode::ADDITIVE: {
                float sum = a + b;
                if (sum > 1.0f) sum = 1.0f;
                if (sum < -1.0f) sum = -1.0f;
                return sum;
            }

            case MixMode::AVERAGE:
                return (a + b) * 0.5f;

            case MixMode::MAX_MAGNITUDE: {
                float abs_a = a < 0.0f ? -a : a;
                float abs_b = b < 0.0f ? -b : b;
                return (abs_a >= abs_b) ? a : b;
            }
        }
        return a;
    }

    MixRule m_rules[kMaxRules];
    uint8_t m_ruleCount;
};

} // namespace bluetooth
} // namespace chopper
