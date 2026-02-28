// Host-side compilation test for the Bluetooth multi-controller system.
// Compile: g++ -std=c++20 -I test/mocks -I main/include test/test_bluetooth.cpp \
//          -o test/test_bluetooth -pthread

#include <cstdio>
#include <cassert>
#include <cstring>

#include "chopper/chopper_limits.h"
#include "chopper/bluetooth/ControllerSlot.h"
#include "chopper/bluetooth/ButtonMappingProfile.h"
#include "chopper/bluetooth/InputMixer.h"
#include "chopper/bluetooth/RoleManager.h"
#include "chopper/bluetooth/ControllerManager.h"

// ---- Test helpers ----

static int test_count = 0;
static int pass_count = 0;

#define TEST(name) \
    do { test_count++; printf("TEST: %s ... ", #name); } while(0)
#define PASS() \
    do { pass_count++; printf("PASS\n"); } while(0)
#define ASSERT(cond) \
    do { if (!(cond)) { printf("FAIL at %s:%d: %s\n", __FILE__, __LINE__, #cond); return; } } while(0)
#define ASSERT_EQ(a, b) \
    do { if ((a) != (b)) { printf("FAIL at %s:%d: %s != %s\n", __FILE__, __LINE__, #a, #b); return; } } while(0)
#define ASSERT_FLOAT_EQ(a, b) \
    do { float _a = (a), _b = (b); if (_a < _b - 0.001f || _a > _b + 0.001f) { \
        printf("FAIL at %s:%d: %s=%.4f != %s=%.4f\n", __FILE__, __LINE__, #a, _a, #b, _b); return; } } while(0)
#define ASSERT_FLOAT_NEAR(a, b, tol) \
    do { float _a = (a), _b = (b); if (_a < _b - (tol) || _a > _b + (tol)) { \
        printf("FAIL at %s:%d: %s=%.4f not near %s=%.4f (tol=%.4f)\n", __FILE__, __LINE__, #a, _a, #b, _b, (float)(tol)); return; } } while(0)

// Helper: create a test MAC address
static chopper::bluetooth::MacAddress makeMac(uint8_t b0, uint8_t b1, uint8_t b2,
                                               uint8_t b3, uint8_t b4, uint8_t b5) {
    chopper::bluetooth::MacAddress m;
    m.addr[0] = b0; m.addr[1] = b1; m.addr[2] = b2;
    m.addr[3] = b3; m.addr[4] = b4; m.addr[5] = b5;
    return m;
}

// ===== ControllerSlot Tests =====

void test_role_to_string() {
    TEST(role_to_string);
    using namespace chopper::bluetooth;
    ASSERT(strcmp(roleToString(ControllerRole::UNASSIGNED), "UNASSIGNED") == 0);
    ASSERT(strcmp(roleToString(ControllerRole::DRIVE), "DRIVE") == 0);
    ASSERT(strcmp(roleToString(ControllerRole::DOME), "DOME") == 0);
    ASSERT(strcmp(roleToString(ControllerRole::ANIMATION), "ANIMATION") == 0);
    ASSERT(strcmp(roleToString(ControllerRole::CAMERA), "CAMERA") == 0);
    PASS();
}

void test_role_to_led_mask() {
    TEST(role_to_led_mask);
    using namespace chopper::bluetooth;
    ASSERT_EQ(roleToLedMask(ControllerRole::DRIVE), (uint8_t)1);
    ASSERT_EQ(roleToLedMask(ControllerRole::DOME), (uint8_t)3);
    ASSERT_EQ(roleToLedMask(ControllerRole::ANIMATION), (uint8_t)7);
    ASSERT_EQ(roleToLedMask(ControllerRole::CAMERA), (uint8_t)15);
    PASS();
}

void test_mac_address() {
    TEST(mac_address_operations);
    using namespace chopper::bluetooth;

    MacAddress a = makeMac(0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF);
    MacAddress b = makeMac(0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF);
    MacAddress c = makeMac(0x11, 0x22, 0x33, 0x44, 0x55, 0x66);
    MacAddress z;

    ASSERT(a == b);
    ASSERT(a != c);
    ASSERT(z.isZero());
    ASSERT(!a.isZero());

    char buf[18];
    a.format(buf, sizeof(buf));
    ASSERT(strcmp(buf, "AA:BB:CC:DD:EE:FF") == 0);

    PASS();
}

void test_slot_initial_state() {
    TEST(slot_initial_state);
    using namespace chopper::bluetooth;

    ControllerSlot slot;
    slot.fullReset();
    ASSERT(slot.isEmpty());
    ASSERT(!slot.isActive());
    ASSERT_EQ(slot.state, ControllerSlot::State::EMPTY);
    ASSERT_EQ(slot.role, ControllerRole::UNASSIGNED);
    ASSERT(slot.mac.isZero());

    PASS();
}

void test_slot_state_strings() {
    TEST(slot_state_strings);
    using namespace chopper::bluetooth;

    ASSERT(strcmp(ControllerSlot::stateToString(ControllerSlot::State::EMPTY), "EMPTY") == 0);
    ASSERT(strcmp(ControllerSlot::stateToString(ControllerSlot::State::ACTIVE), "ACTIVE") == 0);
    ASSERT(strcmp(ControllerSlot::stateToString(ControllerSlot::State::DISCONNECTING), "DISCONNECTING") == 0);

    PASS();
}

void test_slot_disconnect_and_reconnect() {
    TEST(slot_disconnect_and_reconnect_role_hold);
    using namespace chopper::bluetooth;

    ControllerSlot slot;
    slot.fullReset();
    slot.role = ControllerRole::DRIVE;
    slot.state = ControllerSlot::State::ACTIVE;

    // Disconnect at time 1000
    slot.beginDisconnect(1000);
    ASSERT_EQ(slot.state, ControllerSlot::State::DISCONNECTING);
    ASSERT_EQ(slot.previous_role, ControllerRole::DRIVE);

    // Can reclaim within 30 seconds
    ASSERT(slot.canReclaimPreviousRole(2000));
    ASSERT(slot.canReclaimPreviousRole(30999));

    // Cannot reclaim after 30 seconds
    ASSERT(!slot.canReclaimPreviousRole(31001));

    // After clear(), slot is EMPTY but previous_role is preserved
    slot.clear();
    ASSERT(slot.isEmpty());
    ASSERT_EQ(slot.previous_role, ControllerRole::DRIVE);

    // After fullReset(), everything is cleared
    slot.fullReset();
    ASSERT_EQ(slot.previous_role, ControllerRole::UNASSIGNED);

    PASS();
}

// ===== ButtonMappingProfile Tests =====

void test_profile_lookup() {
    TEST(button_mapping_profile_lookup);
    using namespace chopper::bluetooth;

    auto* ps5 = findProfile(ControllerType::kPS5Controller);
    ASSERT(ps5 != nullptr);
    ASSERT(strcmp(ps5->name, "PS5 DualSense") == 0);
    ASSERT(ps5->has_analog_triggers);
    ASSERT(ps5->invert_right_y);

    auto* switchPro = findProfile(ControllerType::kSwitchProController);
    ASSERT(switchPro != nullptr);
    ASSERT(strcmp(switchPro->name, "Switch Pro") == 0);
    ASSERT(!switchPro->has_analog_triggers);

    // Unknown type returns generic
    auto* generic = findProfile(999);
    ASSERT(generic != nullptr);
    ASSERT(strcmp(generic->name, "Generic") == 0);

    PASS();
}

void test_profile_normalize_axis() {
    TEST(button_mapping_profile_normalize_axis);
    using namespace chopper::bluetooth;

    auto* ps5 = findProfile(ControllerType::kPS5Controller);
    ASSERT(ps5 != nullptr);

    // Within deadband -> 0
    ASSERT_FLOAT_EQ(ps5->normalizeAxis(0), 0.0f);
    ASSERT_FLOAT_EQ(ps5->normalizeAxis(10), 0.0f);
    ASSERT_FLOAT_EQ(ps5->normalizeAxis(-10), 0.0f);

    // Full positive
    ASSERT_FLOAT_NEAR(ps5->normalizeAxis(512), 1.0f, 0.01f);

    // Full negative
    ASSERT_FLOAT_NEAR(ps5->normalizeAxis(-512), -1.0f, 0.01f);

    // Half range
    float half = ps5->normalizeAxis(263);  // ~(512-15)/2 + 15
    ASSERT(half > 0.4f && half < 0.6f);

    PASS();
}

void test_profile_deadband() {
    TEST(button_mapping_profile_deadband);
    using namespace chopper::bluetooth;

    // Switch has deadband=20
    auto* sw = findProfile(ControllerType::kSwitchProController);
    ASSERT(sw != nullptr);

    ASSERT_FLOAT_EQ(sw->normalizeAxis(0), 0.0f);
    ASSERT_FLOAT_EQ(sw->normalizeAxis(19), 0.0f);
    ASSERT_FLOAT_EQ(sw->normalizeAxis(-19), 0.0f);

    // Just outside deadband should be non-zero
    float val = sw->normalizeAxis(21);
    ASSERT(val > 0.0f);

    PASS();
}

// ===== InputMixer Tests =====

void test_input_mixer_priority() {
    TEST(input_mixer_priority_mode);
    using namespace chopper::bluetooth;
    using namespace chopper::messages;

    InputMixer mixer;
    InputMixer::MixRule rule = {};
    rule.role_a = ControllerRole::DOME;
    rule.role_b = ControllerRole::DRIVE;
    rule.mode = InputMixer::MixMode::PRIORITY;
    rule.axis_mask = InputMixer::kAxisRX;
    rule.button_mask = 0;

    ASSERT(mixer.addRule(rule));
    ASSERT_EQ(mixer.getRuleCount(), (uint8_t)1);

    ControllerInput a{};
    a.axis_rx_normalized = 0.5f;
    ControllerInput b{};
    b.axis_rx_normalized = 0.8f;

    // a has priority and non-zero -> use a
    ControllerInput result = InputMixer::mix(a, b, rule);
    ASSERT_FLOAT_EQ(result.axis_rx_normalized, 0.5f);

    // a is near zero -> use b
    a.axis_rx_normalized = 0.0f;
    result = InputMixer::mix(a, b, rule);
    ASSERT_FLOAT_EQ(result.axis_rx_normalized, 0.8f);

    PASS();
}

void test_input_mixer_additive() {
    TEST(input_mixer_additive_mode);
    using namespace chopper::bluetooth;
    using namespace chopper::messages;

    InputMixer::MixRule rule = {};
    rule.mode = InputMixer::MixMode::ADDITIVE;
    rule.axis_mask = InputMixer::kAxisX;

    ControllerInput a{};
    a.axis_x_normalized = 0.6f;
    ControllerInput b{};
    b.axis_x_normalized = 0.7f;

    // 0.6 + 0.7 = 1.3 -> clamped to 1.0
    ControllerInput result = InputMixer::mix(a, b, rule);
    ASSERT_FLOAT_EQ(result.axis_x_normalized, 1.0f);

    // Negative clamping
    a.axis_x_normalized = -0.8f;
    b.axis_x_normalized = -0.5f;
    result = InputMixer::mix(a, b, rule);
    ASSERT_FLOAT_EQ(result.axis_x_normalized, -1.0f);

    PASS();
}

void test_input_mixer_average() {
    TEST(input_mixer_average_mode);
    using namespace chopper::bluetooth;
    using namespace chopper::messages;

    InputMixer::MixRule rule = {};
    rule.mode = InputMixer::MixMode::AVERAGE;
    rule.axis_mask = InputMixer::kAxisY;

    ControllerInput a{};
    a.axis_y_normalized = 0.4f;
    ControllerInput b{};
    b.axis_y_normalized = 0.8f;

    ControllerInput result = InputMixer::mix(a, b, rule);
    ASSERT_FLOAT_EQ(result.axis_y_normalized, 0.6f);

    PASS();
}

void test_input_mixer_max_magnitude() {
    TEST(input_mixer_max_magnitude_mode);
    using namespace chopper::bluetooth;
    using namespace chopper::messages;

    InputMixer::MixRule rule = {};
    rule.mode = InputMixer::MixMode::MAX_MAGNITUDE;
    rule.axis_mask = InputMixer::kAxisRY;

    ControllerInput a{};
    a.axis_ry_normalized = -0.9f;
    ControllerInput b{};
    b.axis_ry_normalized = 0.5f;

    // |-0.9| > |0.5| -> use a
    ControllerInput result = InputMixer::mix(a, b, rule);
    ASSERT_FLOAT_EQ(result.axis_ry_normalized, -0.9f);

    PASS();
}

void test_input_mixer_button_or() {
    TEST(input_mixer_button_or_logic);
    using namespace chopper::bluetooth;
    using namespace chopper::messages;

    InputMixer::MixRule rule = {};
    rule.mode = InputMixer::MixMode::PRIORITY;
    rule.axis_mask = 0;
    rule.button_mask = 0x0003;  // Mix bits 0 and 1

    ControllerInput a{};
    a.buttons = 0x0001;   // bit 0 set
    ControllerInput b{};
    b.buttons = 0x0002;   // bit 1 set

    ControllerInput result = InputMixer::mix(a, b, rule);
    ASSERT_EQ(result.buttons, (uint16_t)0x0003);  // OR of bits 0 and 1

    PASS();
}

// ===== RoleManager Tests =====

void test_mac_based_policy() {
    TEST(mac_based_policy);
    using namespace chopper::bluetooth;

    MacBasedPolicy policy;
    MacAddress mac_drive = makeMac(0xAA, 0xBB, 0xCC, 0x01, 0x02, 0x03);
    MacAddress mac_dome  = makeMac(0xAA, 0xBB, 0xCC, 0x04, 0x05, 0x06);

    policy.addMapping(mac_drive, ControllerRole::DRIVE);
    policy.addMapping(mac_dome, ControllerRole::DOME);

    ControllerSlot slots[4];
    for (auto& s : slots) s.fullReset();

    RoleManager mgr;
    mgr.setSlots(slots, 4);
    mgr.setPolicy(policy.getPolicy());

    // Connect the drive controller
    slots[0].mac = mac_drive;
    slots[0].state = ControllerSlot::State::ASSIGNING;
    ControllerRole role = mgr.onControllerAdded(0, 1000);
    ASSERT_EQ(role, ControllerRole::DRIVE);
    ASSERT_EQ(slots[0].role, ControllerRole::DRIVE);

    // Connect the dome controller
    slots[1].mac = mac_dome;
    slots[1].state = ControllerSlot::State::ASSIGNING;
    role = mgr.onControllerAdded(1, 1001);
    ASSERT_EQ(role, ControllerRole::DOME);

    // Unknown MAC gets UNASSIGNED
    MacAddress mac_unknown = makeMac(0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF);
    slots[2].mac = mac_unknown;
    slots[2].state = ControllerSlot::State::ASSIGNING;
    role = mgr.onControllerAdded(2, 1002);
    ASSERT_EQ(role, ControllerRole::UNASSIGNED);

    PASS();
}

void test_mac_based_policy_conflict() {
    TEST(mac_based_policy_role_conflict);
    using namespace chopper::bluetooth;

    MacBasedPolicy policy;
    MacAddress mac1 = makeMac(0x01, 0x02, 0x03, 0x04, 0x05, 0x06);
    MacAddress mac2 = makeMac(0x11, 0x22, 0x33, 0x44, 0x55, 0x66);

    // Both mapped to DRIVE
    policy.addMapping(mac1, ControllerRole::DRIVE);
    policy.addMapping(mac2, ControllerRole::DRIVE);

    ControllerSlot slots[4];
    for (auto& s : slots) s.fullReset();

    RoleManager mgr;
    mgr.setSlots(slots, 4);
    mgr.setPolicy(policy.getPolicy());

    // First gets DRIVE
    slots[0].mac = mac1;
    slots[0].state = ControllerSlot::State::ASSIGNING;
    ControllerRole role = mgr.onControllerAdded(0, 1000);
    ASSERT_EQ(role, ControllerRole::DRIVE);

    // Second gets UNASSIGNED (conflict)
    slots[1].mac = mac2;
    slots[1].state = ControllerSlot::State::ASSIGNING;
    role = mgr.onControllerAdded(1, 1001);
    ASSERT_EQ(role, ControllerRole::UNASSIGNED);

    PASS();
}

void test_first_available_policy() {
    TEST(first_available_policy);
    using namespace chopper::bluetooth;

    FirstAvailablePolicy policy;

    ControllerSlot slots[4];
    for (auto& s : slots) s.fullReset();

    RoleManager mgr;
    mgr.setSlots(slots, 4);
    mgr.setPolicy(policy.getPolicy());

    // Connect 4 controllers — should get DRIVE, DOME, ANIMATION, CAMERA in order
    MacAddress macs[4] = {
        makeMac(0x01, 0, 0, 0, 0, 1),
        makeMac(0x02, 0, 0, 0, 0, 2),
        makeMac(0x03, 0, 0, 0, 0, 3),
        makeMac(0x04, 0, 0, 0, 0, 4),
    };

    ControllerRole expected[4] = {
        ControllerRole::DRIVE,
        ControllerRole::DOME,
        ControllerRole::ANIMATION,
        ControllerRole::CAMERA,
    };

    for (int i = 0; i < 4; i++) {
        slots[i].mac = macs[i];
        slots[i].state = ControllerSlot::State::ASSIGNING;
        ControllerRole role = mgr.onControllerAdded(i, 1000 + i);
        ASSERT_EQ(role, expected[i]);
    }

    PASS();
}

void test_first_available_with_preference() {
    TEST(first_available_policy_with_preference);
    using namespace chopper::bluetooth;

    FirstAvailablePolicy policy;
    MacAddress mac_cam = makeMac(0xCA, 0x00, 0x00, 0x00, 0x00, 0x01);
    policy.addPreference(mac_cam, ControllerRole::CAMERA);

    ControllerSlot slots[4];
    for (auto& s : slots) s.fullReset();

    RoleManager mgr;
    mgr.setSlots(slots, 4);
    mgr.setPolicy(policy.getPolicy());

    // Camera-preferred MAC should get CAMERA even though DRIVE is first available
    slots[0].mac = mac_cam;
    slots[0].state = ControllerSlot::State::ASSIGNING;
    ControllerRole role = mgr.onControllerAdded(0, 1000);
    ASSERT_EQ(role, ControllerRole::CAMERA);

    PASS();
}

void test_manual_policy() {
    TEST(manual_policy);
    using namespace chopper::bluetooth;

    ManualPolicy policy;

    ControllerSlot slots[4];
    for (auto& s : slots) s.fullReset();

    RoleManager mgr;
    mgr.setSlots(slots, 4);
    mgr.setPolicy(policy.getPolicy());

    slots[0].mac = makeMac(0x01, 0, 0, 0, 0, 1);
    slots[0].state = ControllerSlot::State::ASSIGNING;
    ControllerRole role = mgr.onControllerAdded(0, 1000);
    ASSERT_EQ(role, ControllerRole::UNASSIGNED);

    // Manual assignment
    ASSERT(mgr.assignRole(0, ControllerRole::DRIVE));
    ASSERT_EQ(slots[0].role, ControllerRole::DRIVE);
    ASSERT_EQ(mgr.getSlotForRole(ControllerRole::DRIVE), (int8_t)0);

    PASS();
}

void test_role_manager_swap() {
    TEST(role_manager_swap_roles);
    using namespace chopper::bluetooth;

    ControllerSlot slots[4];
    for (auto& s : slots) s.fullReset();

    slots[0].state = ControllerSlot::State::ACTIVE;
    slots[0].role = ControllerRole::DRIVE;
    slots[1].state = ControllerSlot::State::ACTIVE;
    slots[1].role = ControllerRole::DOME;

    RoleManager mgr;
    mgr.setSlots(slots, 4);

    ASSERT(mgr.swapRoles(0, 1));
    ASSERT_EQ(slots[0].role, ControllerRole::DOME);
    ASSERT_EQ(slots[1].role, ControllerRole::DRIVE);

    // Swap with inactive slot fails
    slots[2].state = ControllerSlot::State::EMPTY;
    ASSERT(!mgr.swapRoles(0, 2));

    PASS();
}

void test_role_manager_unassign() {
    TEST(role_manager_unassign);
    using namespace chopper::bluetooth;

    ControllerSlot slots[4];
    for (auto& s : slots) s.fullReset();

    slots[0].state = ControllerSlot::State::ACTIVE;
    slots[0].role = ControllerRole::DRIVE;

    RoleManager mgr;
    mgr.setSlots(slots, 4);

    mgr.unassignRole(0);
    ASSERT_EQ(slots[0].role, ControllerRole::UNASSIGNED);
    ASSERT_EQ(mgr.getSlotForRole(ControllerRole::DRIVE), (int8_t)-1);

    PASS();
}

void test_role_manager_assign_conflict() {
    TEST(role_manager_assign_conflict);
    using namespace chopper::bluetooth;

    ControllerSlot slots[4];
    for (auto& s : slots) s.fullReset();

    slots[0].state = ControllerSlot::State::ACTIVE;
    slots[0].role = ControllerRole::DRIVE;
    slots[1].state = ControllerSlot::State::ACTIVE;
    slots[1].role = ControllerRole::UNASSIGNED;

    RoleManager mgr;
    mgr.setSlots(slots, 4);

    // Cannot assign DRIVE to slot 1 when slot 0 has it
    ASSERT(!mgr.assignRole(1, ControllerRole::DRIVE));
    ASSERT_EQ(slots[1].role, ControllerRole::UNASSIGNED);

    PASS();
}

// ===== ControllerManager Tests =====

void test_controller_manager_connect() {
    TEST(controller_manager_connect);
    using namespace chopper::bluetooth;

    ControllerManager mgr;

    FirstAvailablePolicy policy;
    mgr.getRoleManager().setPolicy(policy.getPolicy());

    MacAddress mac = makeMac(0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x01);
    int8_t slot = mgr.onConnect(mac, ControllerType::kPS5Controller, 0x054C, 0x0CE6, 1000);

    ASSERT(slot >= 0);
    ASSERT_EQ(mgr.getSlot(slot).state, ControllerSlot::State::ACTIVE);
    ASSERT_EQ(mgr.getSlot(slot).role, ControllerRole::DRIVE);
    ASSERT_EQ(mgr.getActiveCount(), (uint8_t)1);

    // Profile lookup
    auto* profile = mgr.getProfileForSlot(slot);
    ASSERT(profile != nullptr);
    ASSERT(strcmp(profile->name, "PS5 DualSense") == 0);

    PASS();
}

void test_controller_manager_multi_connect() {
    TEST(controller_manager_multiple_connections);
    using namespace chopper::bluetooth;

    ControllerManager mgr;

    FirstAvailablePolicy policy;
    mgr.getRoleManager().setPolicy(policy.getPolicy());

    for (int i = 0; i < 4; i++) {
        MacAddress mac = makeMac(0xAA, 0xBB, 0xCC, 0xDD, 0xEE, i + 1);
        int8_t slot = mgr.onConnect(mac, ControllerType::kPS5Controller, 0, 0, 1000 + i);
        ASSERT(slot >= 0);
    }

    ASSERT_EQ(mgr.getActiveCount(), (uint8_t)4);

    // Fifth connection should fail (all slots full)
    MacAddress mac5 = makeMac(0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x05);
    int8_t slot5 = mgr.onConnect(mac5, ControllerType::kPS5Controller, 0, 0, 2000);
    ASSERT_EQ(slot5, (int8_t)-1);

    PASS();
}

void test_controller_manager_disconnect() {
    TEST(controller_manager_disconnect);
    using namespace chopper::bluetooth;

    ControllerManager mgr;
    mgr.setTimeoutMs(0);  // Disable watchdog for this test

    FirstAvailablePolicy policy;
    mgr.getRoleManager().setPolicy(policy.getPolicy());

    DefaultDisconnectHandler handler;
    mgr.setDisconnectBehavior(handler.getBehavior());

    MacAddress mac = makeMac(0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x01);
    int8_t slot = mgr.onConnect(mac, ControllerType::kPS5Controller, 0, 0, 1000);
    ASSERT(slot >= 0);
    ASSERT_EQ(mgr.getSlot(slot).role, ControllerRole::DRIVE);

    // Record some input
    chopper::messages::ControllerInput input{};
    input.axis_x_normalized = 0.5f;
    input.is_connected = true;
    mgr.recordInput(slot, 1100, input);

    // Disconnect (DRIVE has 0ms fallback, so immediate clear)
    mgr.onDisconnect(slot, 1200);
    ASSERT(mgr.getSlot(slot).isEmpty());
    ASSERT_EQ(mgr.getActiveCount(), (uint8_t)0);

    PASS();
}

void test_controller_manager_disconnect_animation_fallback() {
    TEST(controller_manager_animation_fallback);
    using namespace chopper::bluetooth;

    ControllerManager mgr;
    mgr.setTimeoutMs(0);  // Disable watchdog

    // Use mac-based so we can assign ANIMATION directly
    MacBasedPolicy macPolicy;
    MacAddress mac = makeMac(0x11, 0x22, 0x33, 0x44, 0x55, 0x66);
    macPolicy.addMapping(mac, ControllerRole::ANIMATION);
    mgr.getRoleManager().setPolicy(macPolicy.getPolicy());

    DefaultDisconnectHandler handler;
    mgr.setDisconnectBehavior(handler.getBehavior());

    int8_t slot = mgr.onConnect(mac, ControllerType::kPS5Controller, 0, 0, 1000);
    ASSERT(slot >= 0);
    ASSERT_EQ(mgr.getSlot(slot).role, ControllerRole::ANIMATION);

    // Record some input
    chopper::messages::ControllerInput input{};
    input.button_a = true;
    input.is_connected = true;
    mgr.recordInput(slot, 1100, input);

    // Disconnect — ANIMATION has 2000ms fallback
    mgr.onDisconnect(slot, 1200);
    // Slot should still be in DISCONNECTING (fallback active)
    ASSERT(!mgr.getSlot(slot).isEmpty());

    // Fallback should copy the last input
    chopper::messages::ControllerInput fallback{};
    ASSERT(mgr.getFallbackInput(slot, fallback));
    ASSERT(!fallback.is_connected);
    ASSERT(fallback.button_a);  // Last state preserved

    // After 2000ms, update should clear
    mgr.update(3200);
    ASSERT(mgr.getSlot(slot).isEmpty());

    // No more fallback
    ASSERT(!mgr.getFallbackInput(slot, fallback));

    PASS();
}

void test_controller_manager_watchdog() {
    TEST(controller_manager_watchdog_timeout);
    using namespace chopper::bluetooth;

    ControllerManager mgr;
    mgr.setTimeoutMs(200);

    FirstAvailablePolicy policy;
    mgr.getRoleManager().setPolicy(policy.getPolicy());

    DefaultDisconnectHandler handler;
    mgr.setDisconnectBehavior(handler.getBehavior());

    MacAddress mac = makeMac(0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x01);
    int8_t slot = mgr.onConnect(mac, ControllerType::kPS5Controller, 0, 0, 1000);
    ASSERT(slot >= 0);

    // Record input at t=1000
    chopper::messages::ControllerInput input{};
    mgr.recordInput(slot, 1000, input);

    // Update at t=1100 — within timeout, should be fine
    mgr.update(1100);
    ASSERT(mgr.getSlot(slot).isActive());

    // Update at t=1300 — 300ms since last input, exceeds 200ms timeout
    mgr.update(1300);
    ASSERT(mgr.getSlot(slot).isEmpty());  // DRIVE = immediate disconnect

    PASS();
}

void test_controller_manager_reconnect() {
    TEST(controller_manager_reconnect_role_reclaim);
    using namespace chopper::bluetooth;

    ControllerManager mgr;
    mgr.setTimeoutMs(0);

    FirstAvailablePolicy policy;
    mgr.getRoleManager().setPolicy(policy.getPolicy());

    DefaultDisconnectHandler handler;
    mgr.setDisconnectBehavior(handler.getBehavior());

    MacAddress mac = makeMac(0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x01);

    // Connect, gets DRIVE
    int8_t slot = mgr.onConnect(mac, ControllerType::kPS5Controller, 0, 0, 1000);
    ASSERT_EQ(mgr.getSlot(slot).role, ControllerRole::DRIVE);

    // Disconnect
    mgr.onDisconnect(slot, 2000);
    ASSERT(mgr.getSlot(slot).isEmpty());

    // Note: The slot preserves previous_role after clear().
    // But findEmptySlot might give a different slot index.
    // The reconnection reclaim relies on the ControllerSlot's canReclaimPreviousRole.
    // In a real system the registry would match by MAC. For simplicity here,
    // we just verify that the slot's previous_role field is preserved.

    // The slot still remembers previous_role=DRIVE
    ASSERT_EQ(mgr.getSlot(slot).previous_role, ControllerRole::DRIVE);
    ASSERT_EQ(mgr.getSlot(slot).disconnect_time_ms, (uint64_t)2000);

    // Reconnect the same MAC to the same slot within 30s
    // Set up the slot mac before calling onControllerAdded indirectly
    mgr.getSlot(slot).mac = mac;
    mgr.getSlot(slot).state = ControllerSlot::State::ASSIGNING;
    ControllerRole reclaimed = mgr.getRoleManager().onControllerAdded(slot, 3000);
    ASSERT_EQ(reclaimed, ControllerRole::DRIVE);  // Reclaimed!

    PASS();
}

void test_controller_manager_emergency_stop() {
    TEST(controller_manager_emergency_stop);
    using namespace chopper::bluetooth;

    ControllerManager mgr;
    mgr.setTimeoutMs(0);

    FirstAvailablePolicy policy;
    mgr.getRoleManager().setPolicy(policy.getPolicy());

    MacAddress mac = makeMac(0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x01);
    int8_t slot = mgr.onConnect(mac, ControllerType::kPS5Controller, 0, 0, 1000);

    chopper::messages::ControllerInput input{};
    input.axis_x_normalized = 0.8f;
    mgr.recordInput(slot, 1100, input);

    mgr.emergencyStop();

    // Verify all inputs were zeroed (we can't read lastInputs directly,
    // but we can verify via getFallbackInput returning false)
    chopper::messages::ControllerInput fb{};
    ASSERT(!mgr.getFallbackInput(slot, fb));

    PASS();
}

void test_controller_manager_find_operations() {
    TEST(controller_manager_find_by_mac_and_role);
    using namespace chopper::bluetooth;

    ControllerManager mgr;
    mgr.setTimeoutMs(0);

    FirstAvailablePolicy policy;
    mgr.getRoleManager().setPolicy(policy.getPolicy());

    MacAddress mac1 = makeMac(0x01, 0x02, 0x03, 0x04, 0x05, 0x06);
    MacAddress mac2 = makeMac(0x11, 0x22, 0x33, 0x44, 0x55, 0x66);

    int8_t slot1 = mgr.onConnect(mac1, ControllerType::kPS5Controller, 0, 0, 1000);
    int8_t slot2 = mgr.onConnect(mac2, ControllerType::kSwitchProController, 0, 0, 1001);

    ASSERT_EQ(mgr.findSlotByMac(mac1), slot1);
    ASSERT_EQ(mgr.findSlotByMac(mac2), slot2);

    // Unknown MAC
    MacAddress unknown = makeMac(0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF);
    ASSERT_EQ(mgr.findSlotByMac(unknown), (int8_t)-1);

    // Find by role
    ASSERT_EQ(mgr.findSlotByRole(ControllerRole::DRIVE), slot1);
    ASSERT_EQ(mgr.findSlotByRole(ControllerRole::DOME), slot2);
    ASSERT_EQ(mgr.findSlotByRole(ControllerRole::CAMERA), (int8_t)-1);

    PASS();
}

// ===== Disconnect Handler Tests =====

void test_default_disconnect_drive_zeros() {
    TEST(default_disconnect_drive_produces_zeros);
    using namespace chopper::bluetooth;

    DefaultDisconnectHandler handler;
    auto behavior = handler.getBehavior();

    chopper::messages::ControllerInput lastInput{};
    lastInput.axis_x_normalized = 0.8f;
    lastInput.axis_y_normalized = 0.5f;
    lastInput.is_connected = true;

    chopper::messages::ControllerInput fallback{};
    behavior.getFallback(ControllerRole::DRIVE, lastInput, fallback, behavior.context);

    ASSERT_FLOAT_EQ(fallback.axis_x_normalized, 0.0f);
    ASSERT_FLOAT_EQ(fallback.axis_y_normalized, 0.0f);
    ASSERT(!fallback.is_connected);

    ASSERT_EQ(behavior.getFallbackDurationMs(ControllerRole::DRIVE, behavior.context), (uint32_t)0);

    PASS();
}

void test_default_disconnect_dome_holds() {
    TEST(default_disconnect_dome_holds_position);
    using namespace chopper::bluetooth;

    DefaultDisconnectHandler handler;
    auto behavior = handler.getBehavior();

    chopper::messages::ControllerInput lastInput{};
    lastInput.axis_rx = 200;
    lastInput.axis_ry = -100;
    lastInput.axis_rx_normalized = 0.4f;
    lastInput.axis_ry_normalized = -0.2f;

    chopper::messages::ControllerInput fallback{};
    behavior.getFallback(ControllerRole::DOME, lastInput, fallback, behavior.context);

    ASSERT_EQ(fallback.axis_rx, (int32_t)200);
    ASSERT_EQ(fallback.axis_ry, (int32_t)-100);
    ASSERT_FLOAT_EQ(fallback.axis_rx_normalized, 0.4f);
    ASSERT(!fallback.is_connected);

    PASS();
}

// ---- Main ----

int main() {
    printf("=== Chopper Bluetooth Multi-Controller Tests ===\n\n");

    // ControllerSlot
    test_role_to_string();
    test_role_to_led_mask();
    test_mac_address();
    test_slot_initial_state();
    test_slot_state_strings();
    test_slot_disconnect_and_reconnect();

    // ButtonMappingProfile
    test_profile_lookup();
    test_profile_normalize_axis();
    test_profile_deadband();

    // InputMixer
    test_input_mixer_priority();
    test_input_mixer_additive();
    test_input_mixer_average();
    test_input_mixer_max_magnitude();
    test_input_mixer_button_or();

    // RoleManager
    test_mac_based_policy();
    test_mac_based_policy_conflict();
    test_first_available_policy();
    test_first_available_with_preference();
    test_manual_policy();
    test_role_manager_swap();
    test_role_manager_unassign();
    test_role_manager_assign_conflict();

    // ControllerManager
    test_controller_manager_connect();
    test_controller_manager_multi_connect();
    test_controller_manager_disconnect();
    test_controller_manager_disconnect_animation_fallback();
    test_controller_manager_watchdog();
    test_controller_manager_reconnect();
    test_controller_manager_emergency_stop();
    test_controller_manager_find_operations();

    // Disconnect handler
    test_default_disconnect_drive_zeros();
    test_default_disconnect_dome_holds();

    printf("\n=== Results: %d/%d passed ===\n", pass_count, test_count);
    return (pass_count == test_count) ? 0 : 1;
}
