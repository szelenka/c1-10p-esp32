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
    DOME_RANDOM_TOGGLE,
};

enum class ControlField {
    BUTTON_A,
    BUTTON_B,
    BUTTON_X,
    BUTTON_Y,
    BUTTON_L1,
    BUTTON_R1,
    BUTTON_THUMB_L,
    BUTTON_THUMB_R,
    MISC_SELECT,
    MISC_START,
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
        case ControlField::BUTTON_A:
            return input.button_a;
        case ControlField::BUTTON_B:
            return input.button_b;
        case ControlField::BUTTON_X:
            return input.button_x;
        case ControlField::BUTTON_Y:
            return input.button_y;
        case ControlField::BUTTON_L1:
            return input.button_l1;
        case ControlField::BUTTON_R1:
            return input.button_r1;
        case ControlField::BUTTON_THUMB_L:
            return input.button_thumb_l;
        case ControlField::BUTTON_THUMB_R:
            return input.button_thumb_r;
        case ControlField::MISC_SELECT:
            return input.misc_select;
        case ControlField::MISC_START:
            return input.misc_start;
    }
    return false;
}

inline void applyControlPress(messages::ControllerInput& input, ControlField control) {
    switch (control) {
        case ControlField::BUTTON_A:
            input.button_a = true;
            break;
        case ControlField::BUTTON_B:
            input.button_b = true;
            break;
        case ControlField::BUTTON_X:
            input.button_x = true;
            break;
        case ControlField::BUTTON_Y:
            input.button_y = true;
            break;
        case ControlField::BUTTON_L1:
            input.button_l1 = true;
            break;
        case ControlField::BUTTON_R1:
            input.button_r1 = true;
            break;
        case ControlField::BUTTON_THUMB_L:
            input.button_thumb_l = true;
            break;
        case ControlField::BUTTON_THUMB_R:
            input.button_thumb_r = true;
            break;
        case ControlField::MISC_SELECT:
            input.misc_select = true;
            break;
        case ControlField::MISC_START:
            input.misc_start = true;
            break;
    }
}

inline void applyControlRelease(messages::ControllerInput& input, ControlField control) {
    switch (control) {
        case ControlField::BUTTON_A:
            input.button_a = false;
            break;
        case ControlField::BUTTON_B:
            input.button_b = false;
            break;
        case ControlField::BUTTON_X:
            input.button_x = false;
            break;
        case ControlField::BUTTON_Y:
            input.button_y = false;
            break;
        case ControlField::BUTTON_L1:
            input.button_l1 = false;
            break;
        case ControlField::BUTTON_R1:
            input.button_r1 = false;
            break;
        case ControlField::BUTTON_THUMB_L:
            input.button_thumb_l = false;
            break;
        case ControlField::BUTTON_THUMB_R:
            input.button_thumb_r = false;
            break;
        case ControlField::MISC_SELECT:
            input.misc_select = false;
            break;
        case ControlField::MISC_START:
            input.misc_start = false;
            break;
    }
}

inline ControlField resolveControl(UserIntent intent, const DriveIntentMap& map) {
    switch (intent) {
        case UserIntent::PERISCOPE_UP:
            return map.periscope_up;
        case UserIntent::PERISCOPE_DOWN:
            return map.periscope_down;
        case UserIntent::PERISCOPE_SPIN_LEFT:
            return map.periscope_spin_left;
        case UserIntent::PERISCOPE_SPIN_RIGHT:
            return map.periscope_spin_right;
        case UserIntent::DOME_DOORS_TOGGLE:
            return map.dome_doors_toggle;
        case UserIntent::BODY_UTILITY_TOGGLE:
            return map.body_utility_toggle;
        case UserIntent::CARPET_MODE_TOGGLE:
            return map.carpet_mode_toggle;
        case UserIntent::DOME_RANDOM_TOGGLE:
            return map.periscope_up;  // N/A for drive controller
    }
    return map.periscope_up;
}

inline void applyIntentPress(messages::ControllerInput& input, UserIntent intent, const DriveIntentMap& map) {
    applyControlPress(input, resolveControl(intent, map));
}

inline void applyIntentRelease(messages::ControllerInput& input, UserIntent intent, const DriveIntentMap& map) {
    applyControlRelease(input, resolveControl(intent, map));
}

inline void setDriveIntentsFromRaw(messages::ControllerInput& input, const DriveIntentMap& map) {
    input.has_intents = true;
    input.intent_periscope_up = isControlPressed(input, map.periscope_up);
    input.intent_periscope_down = isControlPressed(input, map.periscope_down);
    input.intent_periscope_spin_left = isControlPressed(input, map.periscope_spin_left);
    input.intent_periscope_spin_right = isControlPressed(input, map.periscope_spin_right);
    input.intent_dome_doors_toggle = isControlPressed(input, map.dome_doors_toggle);
    input.intent_body_utility_toggle = isControlPressed(input, map.body_utility_toggle);
    input.intent_carpet_mode_toggle = isControlPressed(input, map.carpet_mode_toggle);
}

// ── Dome controller intent mapping ──────────────────────────────────

struct DomeIntentMap {
    ControlField neck_toggle = ControlField::BUTTON_THUMB_L;
    ControlField neck_height_up = ControlField::BUTTON_R1;
    ControlField neck_height_down = ControlField::BUTTON_L1;
    ControlField sound_a = ControlField::BUTTON_A;
    ControlField sound_b = ControlField::BUTTON_B;
    ControlField sound_random = ControlField::MISC_START;
    ControlField dome_random_toggle = ControlField::BUTTON_THUMB_R;
};

inline DomeIntentMap defaultDomeIntentMap() {
    return DomeIntentMap{};
}

inline void setDomeIntentsFromRaw(messages::ControllerInput& input, const DomeIntentMap& map) {
    input.has_intents = true;
    input.intent_neck_toggle = isControlPressed(input, map.neck_toggle);
    input.intent_neck_height_up = isControlPressed(input, map.neck_height_up);
    input.intent_neck_height_down = isControlPressed(input, map.neck_height_down);
    input.intent_sound_a = isControlPressed(input, map.sound_a);
    input.intent_sound_b = isControlPressed(input, map.sound_b);
    input.intent_sound_random = isControlPressed(input, map.sound_random);
    input.intent_dome_random_toggle = isControlPressed(input, map.dome_random_toggle);
    // Note: intent_face_tracking_toggle is NOT set here — it requires
    // stateful 2-second hold detection, handled by BluepadInputNode.
}

}  // namespace chopper::input
