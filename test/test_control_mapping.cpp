// Host-side tests for intent-to-control mapping.
//
// Compile:
//   g++ -std=c++20 -I test/mocks -I main/include \
//       test/test_control_mapping.cpp \
//       -o test/test_control_mapping -pthread

#include <cstdio>

#include "chopper/messages/CommonMessages.h"
#include "chopper/input/DriveIntentMapping.h"

static int test_count = 0;
static int pass_count = 0;

#define TEST(name) \
    do { test_count++; std::printf("TEST: %s ... ", #name); } while (0)
#define PASS() \
    do { pass_count++; std::printf("PASS\n"); } while (0)
#define ASSERT(cond) \
    do { if (!(cond)) { std::printf("FAIL at %s:%d: %s\n", __FILE__, __LINE__, #cond); return; } } while (0)

using chopper::input::ControlField;
using chopper::input::DriveIntentMap;
using chopper::input::UserIntent;

void test_default_drive_mapping() {
    TEST(default_drive_mapping);
    const DriveIntentMap map = chopper::input::defaultDriveIntentMap();
    ASSERT(map.periscope_up == ControlField::BUTTON_X);
    ASSERT(map.periscope_down == ControlField::BUTTON_X);
    ASSERT(map.periscope_spin_left == ControlField::BUTTON_A);
    ASSERT(map.periscope_spin_right == ControlField::BUTTON_Y);
    ASSERT(map.dome_doors_toggle == ControlField::MISC_SELECT);
    ASSERT(map.body_utility_toggle == ControlField::BUTTON_B);
    ASSERT(map.carpet_mode_toggle == ControlField::BUTTON_THUMB_L);
    PASS();
}

void test_apply_intent_press_and_release() {
    TEST(apply_intent_press_and_release);
    const DriveIntentMap map = chopper::input::defaultDriveIntentMap();
    chopper::messages::ControllerInput input;

    chopper::input::applyIntentPress(input, UserIntent::PERISCOPE_UP, map);
    ASSERT(input.button_x);
    ASSERT(!input.button_a);

    chopper::input::applyIntentRelease(input, UserIntent::PERISCOPE_UP, map);
    ASSERT(!input.button_x);
    PASS();
}

void test_override_mapping() {
    TEST(override_mapping);
    DriveIntentMap map = chopper::input::defaultDriveIntentMap();
    map.periscope_up = ControlField::BUTTON_A;

    chopper::messages::ControllerInput input;
    chopper::input::applyIntentPress(input, UserIntent::PERISCOPE_UP, map);
    ASSERT(input.button_a);
    ASSERT(!input.button_x);
    PASS();
}

void test_set_drive_intents_from_raw() {
    TEST(set_drive_intents_from_raw);
    const DriveIntentMap map = chopper::input::defaultDriveIntentMap();
    chopper::messages::ControllerInput input;
    input.button_x = true;
    input.button_a = true;

    chopper::input::setDriveIntentsFromRaw(input, map);
    ASSERT(input.has_intents);
    ASSERT(input.intent_periscope_up);
    ASSERT(input.intent_periscope_down);
    ASSERT(input.intent_periscope_spin_left);
    ASSERT(!input.intent_periscope_spin_right);
    PASS();
}

int main() {
    std::printf("=== Control Mapping Tests ===\n");
    test_default_drive_mapping();
    test_apply_intent_press_and_release();
    test_override_mapping();
    test_set_drive_intents_from_raw();

    std::printf("\nPassed %d/%d tests\n", pass_count, test_count);
    return (pass_count == test_count) ? 0 : 1;
}
