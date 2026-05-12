#pragma once

#include "chopper/bluetooth/ButtonMappingProfile.h"
#include "chopper/messages/CommonMessages.h"

namespace chopper::input {

// clang-format off
// ═══════════════════════════════════════════════════════════════════════════════════════════════════════════════════════
// Controller Button Reference (from Bluepad32 parser source)
//
// Bluepad32 normalizes all controllers to an Xbox-style positional layout.
// "Position" = where the button sits on the face diamond when holding the controller normally.
// Physical label varies per controller type.
//
// Table 1 — Standard controllers
// ControlField      | Pos   | Xbox      | PS4/PS5    | Switch Pro
// ──────────────────|───────|───────────|────────────|───────────
// BUTTON_A          | south | A         | × cross    | B
// BUTTON_B          | east  | B         | ○ circle   | A
// BUTTON_X          | west  | X         | □ square   | Y
// BUTTON_Y          | north | Y         | △ triangle | X
// BUTTON_L1         | L bmp | LB        | L1         | L
// BUTTON_R1         | R bmp | RB        | R1         | R
// BUTTON_L2         | L trg | LT        | L2         | ZL
// BUTTON_R2         | R trg | RT        | R2         | ZR
// BUTTON_THUMB_L    | L stk | LS click  | L3         | L click
// BUTTON_THUMB_R    | R stk | RS click  | R3         | R click
// MISC_SELECT       | sel   | View      | Share/Crt  | − (minus)
// MISC_START        | start | Menu      | Options    | + (plus)
// MISC_SYSTEM       | home  | Xbox btn  | PS btn     | Home
// MISC_CAPTURE      | extra | —         | Mute (PS5) | Capture
//
// Table 2 — JoyCon horizontal (Bluepad32 default, held sideways)
// ControlField      | Pos   | JoyCon L (h) | JoyCon R (h)
// ──────────────────|───────|──────────────|─────────────
// BUTTON_A          | south | ←            | A
// BUTTON_B          | east  | ↓            | X
// BUTTON_X          | west  | ↑            | B
// BUTTON_Y          | north | →            | Y
// BUTTON_L1         | L bmp | SL           | SL
// BUTTON_R1         | R bmp | SR           | SR
// BUTTON_L2         | L trg | L            | R
// BUTTON_R2         | R trg | ZL           | ZR
// BUTTON_THUMB_L    | L stk | Stick click  | Stick click
// BUTTON_THUMB_R    | R stk | —            | —
// MISC_SELECT       | sel   | − (minus)    | Home
// MISC_START        | start | Capture      | + (plus)
// MISC_SYSTEM       | home  | —            | —
// MISC_CAPTURE      | extra | —            | —
//
// JoyCon L face buttons are the d-pad arrows (labels printed on hardware).
// JoyCon L/R: Stick click → BUTTON_THUMB_L for both; THUMB_R not available.
// JoyCon L/R: SL/SR are the inner rail buttons; in (h) mode they sit on top/bottom
//             like shoulder buttons; in (v) mode they're on the rail (hard to reach).
// Xbox: LT/RT are analog triggers; digital BUTTON_L2/R2 fires at threshold 32.
// ═══════════════════════════════════════════════════════════════════════════════════════════════════════════════════════
// clang-format on

enum class UserIntent {
    PERISCOPE_UP,
    PERISCOPE_DOWN,
    PERISCOPE_SPIN_LEFT,
    PERISCOPE_SPIN_RIGHT,
    DOME_DOORS_TOGGLE,
    BODY_UTILITY_TOGGLE,
    CARPET_MODE_TOGGLE,
    DOME_ROTATE_LEFT,
    DOME_ROTATE_RIGHT,
    EYE_COLOR_TOGGLE,
    DOME_RANDOM_TOGGLE,
};

enum class ControlField {
    BUTTON_A,
    BUTTON_B,
    BUTTON_X,
    BUTTON_Y,
    BUTTON_L1,
    BUTTON_L2,
    BUTTON_R1,
    BUTTON_R2,
    BUTTON_THUMB_L,
    BUTTON_THUMB_R,
    MISC_SELECT,
    MISC_START,
    MISC_SYSTEM,
    MISC_CAPTURE,
};

struct DriveIntentMap {
    ControlField periscope_up = ControlField::BUTTON_X;
    ControlField periscope_down = ControlField::BUTTON_X;
    ControlField periscope_spin_left = ControlField::BUTTON_A;
    ControlField periscope_spin_right = ControlField::BUTTON_Y;
    ControlField dome_doors_toggle = ControlField::MISC_SELECT;
    ControlField body_utility_toggle = ControlField::BUTTON_B;
    ControlField carpet_mode_toggle = ControlField::BUTTON_THUMB_L;
    ControlField dome_rotate_left = ControlField::BUTTON_L2;
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
        case ControlField::BUTTON_L2:
            return input.button_l2;
        case ControlField::BUTTON_R1:
            return input.button_r1;
        case ControlField::BUTTON_R2:
            return input.button_r2;
        case ControlField::BUTTON_THUMB_L:
            return input.button_thumb_l;
        case ControlField::BUTTON_THUMB_R:
            return input.button_thumb_r;
        case ControlField::MISC_SELECT:
            return input.misc_select;
        case ControlField::MISC_START:
            return input.misc_start;
        case ControlField::MISC_SYSTEM:
            return input.misc_system;
        case ControlField::MISC_CAPTURE:
            return input.misc_capture;
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
        case ControlField::BUTTON_L2:
            input.button_l2 = true;
            break;
        case ControlField::BUTTON_R1:
            input.button_r1 = true;
            break;
        case ControlField::BUTTON_R2:
            input.button_r2 = true;
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
        case ControlField::MISC_SYSTEM:
            input.misc_system = true;
            break;
        case ControlField::MISC_CAPTURE:
            input.misc_capture = true;
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
        case ControlField::BUTTON_L2:
            input.button_l2 = false;
            break;
        case ControlField::BUTTON_R1:
            input.button_r1 = false;
            break;
        case ControlField::BUTTON_R2:
            input.button_r2 = false;
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
        case ControlField::MISC_SYSTEM:
            input.misc_system = false;
            break;
        case ControlField::MISC_CAPTURE:
            input.misc_capture = false;
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
        case UserIntent::DOME_ROTATE_LEFT:
            return map.dome_rotate_left;
        case UserIntent::DOME_ROTATE_RIGHT:
            return map.dome_rotate_left;  // N/A for drive controller
        case UserIntent::EYE_COLOR_TOGGLE:
            return map.dome_rotate_left;  // N/A for drive controller
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
    input.intent_dome_rotate_left = isControlPressed(input, map.dome_rotate_left);
}

// ── Dome controller intent mapping ──────────────────────────────────

struct DomeIntentMap {
    ControlField neck_toggle = ControlField::BUTTON_THUMB_L;
    ControlField neck_height_up = ControlField::BUTTON_R1;
    ControlField neck_height_down = ControlField::BUTTON_L1;
    ControlField sound_a = ControlField::BUTTON_A;
    ControlField sound_b = ControlField::BUTTON_B;
    ControlField sound_random = ControlField::MISC_START;
    ControlField dome_rotate_right = ControlField::BUTTON_L2;
    ControlField eye_color_toggle = ControlField::BUTTON_R2;
    ControlField dome_random_toggle = ControlField::MISC_SELECT;
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
    input.intent_dome_rotate_right = isControlPressed(input, map.dome_rotate_right);
    input.intent_eye_color_toggle = isControlPressed(input, map.eye_color_toggle);
    input.intent_dome_random_toggle = isControlPressed(input, map.dome_random_toggle);
    // Note: intent_face_tracking_toggle is NOT set here — it requires
    // stateful 2-second hold detection, handled by BluepadInputNode.
}

// ── Per-controller-type intent maps ─────────────────────────────────
//
// Bluepad32 remaps JoyCon face buttons for horizontal hold.  On JoyCon R
// the physical-label-to-ControlField mapping is:
//   Physical A → BUTTON_A  (unchanged)
//   Physical B → BUTTON_X  (swapped — B and X trade places)
//   Physical X → BUTTON_B  (swapped)
//   Physical Y → BUTTON_Y  (unchanged)
//
// On JoyCon L the d-pad arrows map as:
//   ← → BUTTON_A,  ↓ → BUTTON_B,  ↑ → BUTTON_X,  → → BUTTON_Y
//
// The factory functions below return maps where each intent fires on
// the same physical label regardless of controller type.  For standard
// controllers (Xbox / PS / Switch Pro) the defaults are unchanged because
// Bluepad32 already normalizes them to a consistent positional layout.

inline DriveIntentMap driveIntentMapForController(uint16_t controller_type) {
    using namespace bluetooth::ControllerType;
    switch (controller_type) {
        case kSwitchJoyConLeft: {
            // ← = BUTTON_A, ↓ = BUTTON_B, ↑ = BUTTON_X, → = BUTTON_Y
            // Default positional mapping is already intuitive for arrows.
            return defaultDriveIntentMap();
        }
        case kSwitchJoyConRight: {
            // Swap B ↔ X so physical labels match standard controllers.
            // Standard:  periscope_up = BUTTON_X (west) → phys B on JoyCon R
            //            body_utility = BUTTON_B (east) → phys X on JoyCon R
            // Swapped:   periscope_up = BUTTON_B (east) → phys X on JoyCon R
            //            body_utility = BUTTON_X (west) → phys B on JoyCon R
            DriveIntentMap map = defaultDriveIntentMap();
            map.periscope_up = ControlField::BUTTON_B;
            map.periscope_down = ControlField::BUTTON_B;
            map.body_utility_toggle = ControlField::BUTTON_X;
            return map;
        }
        default:
            return defaultDriveIntentMap();
    }
}

inline DomeIntentMap domeIntentMapForController(uint16_t controller_type) {
    using namespace bluetooth::ControllerType;
    switch (controller_type) {
        case kSwitchJoyConLeft: {
            return defaultDomeIntentMap();
        }
        case kSwitchJoyConRight: {
            // Swap B ↔ X so physical labels match standard controllers.
            // Standard:  sound_b = BUTTON_B → phys X on JoyCon R
            // Swapped:   sound_b = BUTTON_X → phys B on JoyCon R
            DomeIntentMap map = defaultDomeIntentMap();
            map.sound_b = ControlField::BUTTON_X;
            return map;
        }
        default:
            return defaultDomeIntentMap();
    }
}

}  // namespace chopper::input
