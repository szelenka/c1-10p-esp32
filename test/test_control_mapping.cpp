// Host-side tests for intent-to-control mapping.
//
// Compile:
//   g++ -std=c++20 -I test/mocks -I main/include \
//       test/test_control_mapping.cpp \
//       -o test/test_control_mapping -pthread

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "chopper/messages/CommonMessages.h"
#include "chopper/input/DriveIntentMapping.h"

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
    CHECK(map.dome_random_toggle == ControlField::BUTTON_THUMB_R);
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

TEST_CASE("dome_override_mapping") {
    DomeIntentMap map = chopper::input::defaultDomeIntentMap();
    map.sound_a = ControlField::BUTTON_Y;

    chopper::messages::ControllerInput input;
    input.button_y = true;

    chopper::input::setDomeIntentsFromRaw(input, map);
    CHECK(input.intent_sound_a);
    CHECK_FALSE(input.intent_sound_b);
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
