#pragma once

#include <cstdint>

namespace chopper {
namespace bluetooth {

/**
 * Per-controller-type button mapping and normalization profile.
 *
 * Different controller types (PS4, PS5, Xbox, Switch) have different
 * physical layouts and axis ranges. This profile normalizes them to
 * a consistent interface.
 *
 * Bluepad32 normalizes most controllers to a -512..512 range internally.
 * The profile exists primarily for deadband, inversion defaults, and
 * trigger mode differences.
 */
struct ButtonMappingProfile {
    const char* name;              ///< Human-readable name (e.g., "PS5 DualSense")
    uint16_t controller_type;      ///< Bluepad32 controller type ID

    // Axis configuration
    int32_t axis_min;              ///< Raw axis minimum (e.g., -512)
    int32_t axis_max;              ///< Raw axis maximum (e.g., +512)

    // Axis inversion defaults (some controllers have inverted Y)
    bool invert_left_y;
    bool invert_right_y;

    // Deadband radius (raw units, applied before normalization)
    int32_t deadband;

    // Trigger mode
    bool has_analog_triggers;      ///< PS4/PS5/Xbox = true, Switch = false

    // SlewRateLimiter defaults for this controller type
    float default_slew_positive;
    float default_slew_negative;

    /// Normalize a raw axis value to [-1.0, 1.0] with deadband.
    float normalizeAxis(int32_t raw) const {
        // Apply deadband
        if (raw > -deadband && raw < deadband) {
            return 0.0f;
        }
        // Shift raw value to remove deadband zone
        float adjusted;
        if (raw >= deadband) {
            adjusted = static_cast<float>(raw - deadband);
        } else {
            adjusted = static_cast<float>(raw + deadband);
        }
        float range = static_cast<float>(axis_max - deadband);
        if (range <= 0.0f) return 0.0f;
        float normalized = adjusted / range;
        // Clamp
        if (normalized > 1.0f) normalized = 1.0f;
        if (normalized < -1.0f) normalized = -1.0f;
        return normalized;
    }
};

/// Known controller type IDs from Bluepad32 (subset relevant to this project).
namespace ControllerType {
    static constexpr uint16_t kUnknown              = 0;
    static constexpr uint16_t kSwitchJoyConLeft     = 5;
    static constexpr uint16_t kSwitchJoyConRight    = 6;
    static constexpr uint16_t kSwitchProController  = 7;
    static constexpr uint16_t kPS4Controller        = 9;
    static constexpr uint16_t kPS5Controller        = 10;
    static constexpr uint16_t kXBoxOneController    = 15;
}

/// Built-in profile table. Searched by controller_type.
static constexpr uint8_t kProfileCount = 7;

static const ButtonMappingProfile kBuiltinProfiles[kProfileCount] = {
    {"Switch JoyCon L",  ControllerType::kSwitchJoyConLeft,    -512,  512, false, false, 20, false, 0.75f, -0.75f},
    {"Switch JoyCon R",  ControllerType::kSwitchJoyConRight,   -512,  512, false, false, 20, false, 0.75f, -0.75f},
    {"Switch Pro",       ControllerType::kSwitchProController, -512,  512, false, false, 20, false, 0.75f, -0.75f},
    {"PS4 DualShock",    ControllerType::kPS4Controller,       -512,  512, false, true,  15, true,  0.75f, -0.75f},
    {"PS5 DualSense",    ControllerType::kPS5Controller,       -512,  512, false, true,  15, true,  0.75f, -0.75f},
    {"Xbox One",         ControllerType::kXBoxOneController,   -512,  512, false, true,  15, true,  0.75f, -0.75f},
    {"Generic",          ControllerType::kUnknown,             -512,  512, false, false, 20, false, 0.75f, -0.75f},
};

/**
 * Look up a ButtonMappingProfile by Bluepad32 controller type.
 * Returns the "Generic" profile if no match found.
 */
inline const ButtonMappingProfile* findProfile(uint16_t controllerType) {
    for (uint8_t i = 0; i < kProfileCount; i++) {
        if (kBuiltinProfiles[i].controller_type == controllerType) {
            return &kBuiltinProfiles[i];
        }
    }
    // Return generic (last entry)
    return &kBuiltinProfiles[kProfileCount - 1];
}

} // namespace bluetooth
} // namespace chopper
