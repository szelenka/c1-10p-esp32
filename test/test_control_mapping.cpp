// Host-side tests for intent-to-control mapping.
//
// Compile:
//   g++ -std=c++20 -I test/mocks -I main/include \
//       test/test_control_mapping.cpp \
//       -o test/test_control_mapping -pthread

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include <cstring>

#include "chopper/messages/CommonMessages.h"
#include "chopper/input/ControllerCalibration.h"
#include "chopper/input/DriveIntentMapping.h"
#include "chopper/input/TEmbedInputAdapter.h"

using chopper::input::ControlField;
using chopper::input::DriveIntentMap;
using chopper::input::DomeIntentMap;
using chopper::input::UserIntent;

TEST_CASE("default_drive_mapping") {
    const DriveIntentMap map = chopper::input::defaultDriveIntentMap();
    CHECK(map.periscope_up == ControlField::BUTTON_X);
    CHECK(map.periscope_down == ControlField::BUTTON_X);
    CHECK(map.periscope_spin_left == ControlField::BUTTON_A);
    CHECK(map.periscope_spin_right == ControlField::BUTTON_Y);
    CHECK(map.dome_doors_toggle == ControlField::MISC_SELECT);
    CHECK(map.body_utility_toggle == ControlField::BUTTON_B);
    CHECK(map.carpet_mode_toggle == ControlField::BUTTON_THUMB_L);
    CHECK(map.dome_rotate_left == ControlField::BUTTON_L2);
    CHECK(map.volume_up == ControlField::BUTTON_L1);
    CHECK(map.volume_down == ControlField::BUTTON_R1);
}

TEST_CASE("apply_intent_press_and_release") {
    const DriveIntentMap map = chopper::input::defaultDriveIntentMap();
    chopper::messages::ControllerInput input;

    chopper::input::applyIntentPress(input, UserIntent::PERISCOPE_UP, map);
    CHECK(input.button_x);
    CHECK_FALSE(input.button_a);

    chopper::input::applyIntentRelease(input, UserIntent::PERISCOPE_UP, map);
    CHECK_FALSE(input.button_x);
}

TEST_CASE("override_mapping") {
    DriveIntentMap map = chopper::input::defaultDriveIntentMap();
    map.periscope_up = ControlField::BUTTON_A;

    chopper::messages::ControllerInput input;
    chopper::input::applyIntentPress(input, UserIntent::PERISCOPE_UP, map);
    CHECK(input.button_a);
    CHECK_FALSE(input.button_x);
}

TEST_CASE("unsupported_drive_intents_are_noop") {
    const DriveIntentMap map = chopper::input::defaultDriveIntentMap();
    chopper::messages::ControllerInput input;

    chopper::input::applyIntentPress(input, UserIntent::DOME_ROTATE_RIGHT, map);
    chopper::input::applyIntentPress(input, UserIntent::EYE_COLOR_TOGGLE, map);
    chopper::input::applyIntentPress(input, UserIntent::DOME_RANDOM_TOGGLE, map);

    CHECK_FALSE(input.button_a);
    CHECK_FALSE(input.button_b);
    CHECK_FALSE(input.button_x);
    CHECK_FALSE(input.button_y);
    CHECK_FALSE(input.button_l1);
    CHECK_FALSE(input.button_l2);
    CHECK_FALSE(input.button_r1);
    CHECK_FALSE(input.button_r2);
    CHECK_FALSE(input.misc_select);
    CHECK_FALSE(input.misc_start);
}

TEST_CASE("set_drive_intents_from_raw") {
    const DriveIntentMap map = chopper::input::defaultDriveIntentMap();
    chopper::messages::ControllerInput input;
    input.button_x = true;
    input.button_a = true;

    chopper::input::setDriveIntentsFromRaw(input, map);
    CHECK(input.has_intents);
    CHECK(input.intent_periscope_up);
    CHECK(input.intent_periscope_down);
    CHECK(input.intent_periscope_spin_left);
    CHECK_FALSE(input.intent_periscope_spin_right);
    CHECK_FALSE(input.intent_dome_rotate_left);
    CHECK_FALSE(input.intent_volume_up);
    CHECK_FALSE(input.intent_volume_down);
}

TEST_CASE("set_drive_intents_volume") {
    const DriveIntentMap map = chopper::input::defaultDriveIntentMap();
    chopper::messages::ControllerInput input;
    input.button_l1 = true;
    input.button_r1 = true;

    chopper::input::setDriveIntentsFromRaw(input, map);
    CHECK(input.has_intents);
    CHECK(input.intent_volume_up);
    CHECK(input.intent_volume_down);
    CHECK_FALSE(input.intent_neck_height_up);
    CHECK_FALSE(input.intent_neck_height_down);
}

TEST_CASE("set_drive_intents_dome_rotate_left") {
    const DriveIntentMap map = chopper::input::defaultDriveIntentMap();
    chopper::messages::ControllerInput input;
    input.button_l2 = true;

    chopper::input::setDriveIntentsFromRaw(input, map);
    CHECK(input.has_intents);
    CHECK(input.intent_dome_rotate_left);
    CHECK_FALSE(input.intent_periscope_up);
}

// ── Dome intent mapping ──

TEST_CASE("default_dome_mapping") {
    const DomeIntentMap map = chopper::input::defaultDomeIntentMap();
    CHECK(map.neck_toggle == ControlField::BUTTON_THUMB_L);
    CHECK(map.neck_height_up == ControlField::BUTTON_R1);
    CHECK(map.neck_height_down == ControlField::BUTTON_L1);
    CHECK(map.sound_a == ControlField::BUTTON_A);
    CHECK(map.sound_b == ControlField::BUTTON_B);
    CHECK(map.sound_random == ControlField::MISC_START);
    CHECK(map.dome_rotate_right == ControlField::BUTTON_L2);
    CHECK(map.eye_color_toggle == ControlField::BUTTON_R2);
    CHECK(map.dome_random_toggle == ControlField::MISC_SELECT);
}

TEST_CASE("set_dome_intents_from_raw") {
    const DomeIntentMap map = chopper::input::defaultDomeIntentMap();
    chopper::messages::ControllerInput input;
    input.button_a = true;
    input.button_r1 = true;

    chopper::input::setDomeIntentsFromRaw(input, map);
    CHECK(input.has_intents);
    CHECK(input.intent_sound_a);
    CHECK_FALSE(input.intent_sound_b);
    CHECK_FALSE(input.intent_sound_random);
    CHECK_FALSE(input.intent_neck_toggle);
    CHECK(input.intent_neck_height_up);
    CHECK_FALSE(input.intent_neck_height_down);
    CHECK_FALSE(input.intent_dome_rotate_right);
}

TEST_CASE("set_dome_intents_dome_rotate_right") {
    const DomeIntentMap map = chopper::input::defaultDomeIntentMap();
    chopper::messages::ControllerInput input;
    input.button_l2 = true;

    chopper::input::setDomeIntentsFromRaw(input, map);
    CHECK(input.has_intents);
    CHECK(input.intent_dome_rotate_right);
    CHECK_FALSE(input.intent_sound_a);
}

TEST_CASE("set_dome_intents_eye_color_toggle") {
    const DomeIntentMap map = chopper::input::defaultDomeIntentMap();
    chopper::messages::ControllerInput input;
    input.button_r2 = true;

    chopper::input::setDomeIntentsFromRaw(input, map);
    CHECK(input.has_intents);
    CHECK(input.intent_eye_color_toggle);
    CHECK_FALSE(input.intent_dome_rotate_right);
}

TEST_CASE("dome_override_mapping") {
    DomeIntentMap map = chopper::input::defaultDomeIntentMap();
    map.sound_a = ControlField::BUTTON_Y;

    chopper::messages::ControllerInput input;
    input.button_y = true;

    chopper::input::setDomeIntentsFromRaw(input, map);
    CHECK(input.intent_sound_a);
    CHECK_FALSE(input.intent_sound_b);
}

// ── Per-controller-type factory functions ──

TEST_CASE("drive_map_standard_controller_returns_default") {
    // Xbox, PS4, PS5, Switch Pro, unknown — all return the default map.
    const DriveIntentMap def = chopper::input::defaultDriveIntentMap();
    for (uint16_t type : {0, 7, 9, 10, 15}) {
        const DriveIntentMap map = chopper::input::driveIntentMapForController(type);
        CHECK(map.periscope_up == def.periscope_up);
        CHECK(map.periscope_down == def.periscope_down);
        CHECK(map.body_utility_toggle == def.body_utility_toggle);
    }
}

TEST_CASE("drive_map_joycon_left_returns_default") {
    // JoyCon L d-pad arrows are positionally intuitive — no changes needed.
    const DriveIntentMap def = chopper::input::defaultDriveIntentMap();
    const DriveIntentMap map = chopper::input::driveIntentMapForController(
        chopper::bluetooth::ControllerType::kSwitchJoyConLeft);
    CHECK(map.periscope_up == def.periscope_up);
    CHECK(map.body_utility_toggle == def.body_utility_toggle);
}

TEST_CASE("drive_map_joycon_right_swaps_b_x") {
    // JoyCon R: physical B→BUTTON_X, physical X→BUTTON_B.
    // Factory swaps so physical labels match standard controllers.
    const DriveIntentMap map = chopper::input::driveIntentMapForController(
        chopper::bluetooth::ControllerType::kSwitchJoyConRight);
    // periscope_up was BUTTON_X (default), now BUTTON_B so it fires on phys X
    CHECK(map.periscope_up == ControlField::BUTTON_B);
    CHECK(map.periscope_down == ControlField::BUTTON_B);
    // body_utility was BUTTON_B (default), now BUTTON_X so it fires on phys B
    CHECK(map.body_utility_toggle == ControlField::BUTTON_X);
    // Non-face-button fields unchanged
    CHECK(map.periscope_spin_left == ControlField::BUTTON_A);
    CHECK(map.periscope_spin_right == ControlField::BUTTON_Y);
    CHECK(map.dome_doors_toggle == ControlField::MISC_SELECT);
}

TEST_CASE("dome_map_standard_controller_returns_default") {
    const DomeIntentMap def = chopper::input::defaultDomeIntentMap();
    for (uint16_t type : {0, 7, 9, 10, 15}) {
        const DomeIntentMap map = chopper::input::domeIntentMapForController(type);
        CHECK(map.sound_a == def.sound_a);
        CHECK(map.sound_b == def.sound_b);
    }
}

TEST_CASE("dome_map_joycon_right_swaps_sound_b") {
    // sound_b was BUTTON_B → phys X on JoyCon R.
    // Factory sets BUTTON_X so it fires on phys B instead.
    const DomeIntentMap map = chopper::input::domeIntentMapForController(
        chopper::bluetooth::ControllerType::kSwitchJoyConRight);
    CHECK(map.sound_a == ControlField::BUTTON_A);
    CHECK(map.sound_b == ControlField::BUTTON_X);
    // Other fields unchanged
    CHECK(map.neck_toggle == ControlField::BUTTON_THUMB_L);
    CHECK(map.eye_color_toggle == ControlField::BUTTON_R2);
}

TEST_CASE("joycon_right_drive_intents_from_raw") {
    // Verify end-to-end: pressing physical X (= BUTTON_B on JoyCon R)
    // triggers periscope_up with the JoyCon R map.
    const DriveIntentMap map = chopper::input::driveIntentMapForController(
        chopper::bluetooth::ControllerType::kSwitchJoyConRight);
    chopper::messages::ControllerInput input{};
    input.button_b = true;  // physical X on JoyCon R

    chopper::input::setDriveIntentsFromRaw(input, map);
    CHECK(input.intent_periscope_up);
    CHECK(input.intent_periscope_down);
    CHECK_FALSE(input.intent_body_utility_toggle);
}

TEST_CASE("joycon_right_dome_intents_from_raw") {
    // Pressing physical B (= BUTTON_X on JoyCon R) triggers sound_b.
    const DomeIntentMap map = chopper::input::domeIntentMapForController(
        chopper::bluetooth::ControllerType::kSwitchJoyConRight);
    chopper::messages::ControllerInput input{};
    input.button_x = true;  // physical B on JoyCon R

    chopper::input::setDomeIntentsFromRaw(input, map);
    CHECK(input.intent_sound_b);
    CHECK_FALSE(input.intent_sound_a);
}

TEST_CASE("new_control_fields_round_trip") {
    chopper::messages::ControllerInput input;

    chopper::input::applyControlPress(input, ControlField::BUTTON_L1);
    CHECK(chopper::input::isControlPressed(input, ControlField::BUTTON_L1));

    chopper::input::applyControlPress(input, ControlField::BUTTON_R1);
    CHECK(chopper::input::isControlPressed(input, ControlField::BUTTON_R1));

    chopper::input::applyControlPress(input, ControlField::BUTTON_L2);
    CHECK(chopper::input::isControlPressed(input, ControlField::BUTTON_L2));

    chopper::input::applyControlPress(input, ControlField::BUTTON_R2);
    CHECK(chopper::input::isControlPressed(input, ControlField::BUTTON_R2));

    chopper::input::applyControlPress(input, ControlField::MISC_START);
    CHECK(chopper::input::isControlPressed(input, ControlField::MISC_START));

    chopper::input::applyControlRelease(input, ControlField::BUTTON_L1);
    CHECK_FALSE(chopper::input::isControlPressed(input, ControlField::BUTTON_L1));

    chopper::input::applyControlRelease(input, ControlField::BUTTON_L2);
    CHECK_FALSE(chopper::input::isControlPressed(input, ControlField::BUTTON_L2));
}

TEST_CASE("controller_axis_calibration_matches_legacy_order") {
    CHECK(chopper::input::applyAxisCalibrationRaw(100, -30, true) == -130);
    CHECK(chopper::input::normalizeCalibratedAxis(100, -30, true) == doctest::Approx(-130.0f / 512.0f));
    CHECK(chopper::input::normalizeCalibratedAxis(-800, 0, false) == doctest::Approx(-1.0f));
    CHECK(chopper::input::normalizeCalibratedAxis(800, 0, false) == doctest::Approx(1.0f));
}

TEST_CASE("tembed_mac_detection_matches_expected_controller") {
    CHECK(chopper::input::isTEmbedMac("7C:2C:67:8A:14:0E"));
    CHECK_FALSE(chopper::input::isTEmbedMac("98:E6:B9:62:6E:58"));
}

TEST_CASE("tembed_drive_input_keeps_drive_axes_and_maps_intents") {
    chopper::messages::ControllerInput raw{};
    raw.controller_id = 3;
    raw.battery_level = 77;
    raw.is_connected = true;
    raw.has_data = true;
    raw.is_gamepad = true;
    std::strcpy(raw.mac_address, chopper::input::kTEmbedMac);
    raw.axis_x = 120;
    raw.axis_y = -240;
    raw.axis_x_normalized = 0.25f;
    raw.axis_y_normalized = -0.5f;
    raw.axis_x_slew = 0.2f;
    raw.axis_y_slew = -0.4f;
    raw.axis_rx = 333;
    raw.axis_rx_normalized = 0.65f;
    raw.button_a = true;
    raw.button_b = true;
    raw.button_x = true;
    raw.button_y = true;
    raw.button_l1 = true;
    raw.button_r1 = true;
    raw.misc_select = true;

    const auto drive = chopper::input::makeTEmbedDriveInput(raw);
    CHECK(drive.has_intents);
    CHECK(drive.intent_periscope_up);
    CHECK(drive.intent_periscope_down);
    CHECK(drive.intent_periscope_spin_left);
    CHECK(drive.intent_periscope_spin_right);
    CHECK(drive.intent_dome_doors_toggle);
    CHECK(drive.intent_body_left_door_toggle);
    CHECK(drive.intent_body_right_door_toggle);
    CHECK(drive.intent_body_utility_toggle);
    CHECK_FALSE(drive.intent_sound_a);
    CHECK_FALSE(drive.intent_carpet_mode_toggle);
    CHECK(drive.axis_x == raw.axis_x);
    CHECK(drive.axis_y == raw.axis_y);
    CHECK(drive.axis_x_normalized == doctest::Approx(raw.axis_x_normalized));
    CHECK(drive.axis_y_normalized == doctest::Approx(raw.axis_y_normalized));
    CHECK(drive.axis_x_slew == doctest::Approx(raw.axis_x_slew));
    CHECK(drive.axis_y_slew == doctest::Approx(raw.axis_y_slew));
    CHECK(drive.axis_rx == 0);
    CHECK_FALSE(drive.button_x);
    CHECK_FALSE(drive.misc_select);
    CHECK(std::strcmp(drive.mac_address, chopper::input::kTEmbedMac) == 0);
}

TEST_CASE("tembed_dome_input_keeps_rx_axis_and_maps_remote_actions") {
    chopper::messages::ControllerInput raw{};
    raw.controller_id = 1;
    raw.battery_level = 55;
    raw.is_connected = true;
    raw.has_data = true;
    raw.is_gamepad = true;
    std::strcpy(raw.mac_address, chopper::input::kTEmbedMac);
    raw.axis_x = -200;
    raw.axis_rx = 256;
    raw.axis_rx_normalized = 0.5f;
    raw.button_b = true;
    raw.button_r2 = true;
    raw.misc_select = true;
    raw.misc_start = true;

    const auto dome = chopper::input::makeTEmbedDomeInput(raw);
    CHECK(dome.has_intents);
    CHECK(dome.intent_sound_a);
    CHECK_FALSE(dome.intent_sound_b);
    CHECK(dome.intent_sound_random);
    CHECK(dome.intent_eye_color_toggle);
    CHECK(dome.intent_dome_doors_toggle);
    CHECK_FALSE(dome.intent_dome_random_toggle);
    CHECK_FALSE(dome.intent_neck_toggle);
    CHECK(dome.axis_rx == raw.axis_rx);
    CHECK(dome.axis_rx_normalized == doctest::Approx(raw.axis_rx_normalized));
    CHECK(dome.axis_x == 0);
    CHECK_FALSE(dome.button_a);
    CHECK_FALSE(dome.button_b);
    CHECK_FALSE(dome.misc_select);
    CHECK(std::strcmp(dome.mac_address, chopper::input::kTEmbedMac) == 0);

    raw.button_b = false;
    raw.button_a = true;
    const auto dome_button_a = chopper::input::makeTEmbedDomeInput(raw);
    CHECK_FALSE(dome_button_a.intent_sound_a);
    CHECK(dome_button_a.intent_sound_b);
}
