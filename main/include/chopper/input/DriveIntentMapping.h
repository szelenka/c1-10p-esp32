#pragma once

#include "chopper/messages/CommonMessages.h"

namespace chopper::input {

enum class UserIntent {
    PERISCOPE_UP,
    PERISCOPE_DOWN,
    PERISCOPE_SPIN_LEFT,
    PERISCOPE_SPIN_RIGHT,
    DOME_DOORS_TOGGLE,
    BODY_UTILITY_TOGGLE,
    CARPET_MODE_TOGGLE,
};

enum class ControlField {
    BUTTON_A,
    BUTTON_B,
    BUTTON_X,
    BUTTON_Y,
    BUTTON_THUMB_L,
    MISC_SELECT,
};

struct DriveIntentMap {
    ControlField periscope_up = ControlField::BUTTON_X;
    ControlField periscope_down = ControlField::BUTTON_X;
    ControlField periscope_spin_left = ControlField::BUTTON_A;
    ControlField periscope_spin_right = ControlField::BUTTON_Y;
    ControlField dome_doors_toggle = ControlField::MISC_SELECT;
    ControlField body_utility_toggle = ControlField::BUTTON_B;
    ControlField carpet_mode_toggle = ControlField::BUTTON_THUMB_L;
};

inline DriveIntentMap defaultDriveIntentMap() {
    return DriveIntentMap{};
}

inline bool isControlPressed(const messages::ControllerInput& input, ControlField control) {
    switch (control) {
        case ControlField::BUTTON_A: return input.button_a;
        case ControlField::BUTTON_B: return input.button_b;
        case ControlField::BUTTON_X: return input.button_x;
        case ControlField::BUTTON_Y: return input.button_y;
        case ControlField::BUTTON_THUMB_L: return input.button_thumb_l;
        case ControlField::MISC_SELECT: return input.misc_select;
    }
    return false;
}

inline void applyControlPress(messages::ControllerInput& input, ControlField control) {
    switch (control) {
        case ControlField::BUTTON_A: input.button_a = true; break;
        case ControlField::BUTTON_B: input.button_b = true; break;
        case ControlField::BUTTON_X: input.button_x = true; break;
        case ControlField::BUTTON_Y: input.button_y = true; break;
        case ControlField::BUTTON_THUMB_L: input.button_thumb_l = true; break;
        case ControlField::MISC_SELECT: input.misc_select = true; break;
    }
}

inline void applyControlRelease(messages::ControllerInput& input, ControlField control) {
    switch (control) {
        case ControlField::BUTTON_A: input.button_a = false; break;
        case ControlField::BUTTON_B: input.button_b = false; break;
        case ControlField::BUTTON_X: input.button_x = false; break;
        case ControlField::BUTTON_Y: input.button_y = false; break;
        case ControlField::BUTTON_THUMB_L: input.button_thumb_l = false; break;
        case ControlField::MISC_SELECT: input.misc_select = false; break;
    }
}

inline ControlField resolveControl(UserIntent intent, const DriveIntentMap& map) {
    switch (intent) {
        case UserIntent::PERISCOPE_UP: return map.periscope_up;
        case UserIntent::PERISCOPE_DOWN: return map.periscope_down;
        case UserIntent::PERISCOPE_SPIN_LEFT: return map.periscope_spin_left;
        case UserIntent::PERISCOPE_SPIN_RIGHT: return map.periscope_spin_right;
        case UserIntent::DOME_DOORS_TOGGLE: return map.dome_doors_toggle;
        case UserIntent::BODY_UTILITY_TOGGLE: return map.body_utility_toggle;
        case UserIntent::CARPET_MODE_TOGGLE: return map.carpet_mode_toggle;
    }
    return map.periscope_up;
}

inline void applyIntentPress(messages::ControllerInput& input,
                             UserIntent intent,
                             const DriveIntentMap& map) {
    applyControlPress(input, resolveControl(intent, map));
}

inline void applyIntentRelease(messages::ControllerInput& input,
                               UserIntent intent,
                               const DriveIntentMap& map) {
    applyControlRelease(input, resolveControl(intent, map));
}

inline void setDriveIntentsFromRaw(messages::ControllerInput& input,
                                   const DriveIntentMap& map) {
    input.has_intents = true;
    input.intent_periscope_up = isControlPressed(input, map.periscope_up);
    input.intent_periscope_down = isControlPressed(input, map.periscope_down);
    input.intent_periscope_spin_left = isControlPressed(input, map.periscope_spin_left);
    input.intent_periscope_spin_right = isControlPressed(input, map.periscope_spin_right);
    input.intent_dome_doors_toggle = isControlPressed(input, map.dome_doors_toggle);
    input.intent_body_utility_toggle = isControlPressed(input, map.body_utility_toggle);
    input.intent_carpet_mode_toggle = isControlPressed(input, map.carpet_mode_toggle);
}

}  // namespace chopper::input

