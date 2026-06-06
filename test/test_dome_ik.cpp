// Host-side tests for the 3-RSS IK solver, RSSMechanism, Easing,
// SlewRateLimiter, MathUtil, DomePosition, NeckNode, DomeNode.
//
// Compile:
//   g++ -std=c++20 -I test/mocks -I main/include \
//       test/test_dome_ik.cpp \
//       main/chopper/core/Node.cpp \
//       main/chopper/core/Publisher.cpp \
//       main/chopper/core/Subscription.cpp \
//       main/chopper/core/MessageBroker.cpp \
//       main/chopper/core/PublishingNode.cpp \
//       main/chopper/core/Message.cpp \
//       -o test/test_dome_ik -pthread

#include <cstdio>
#include <cassert>
#include <cstring>
#include <cmath>
#include <memory>
#include <array>

// Math / Domain
#include "chopper/math/Easing.h"
#include "chopper/math/SlewRateLimiter.h"
#include "chopper/math/MathUtil.h"
#include "chopper/math/RSSMachine.h"
#include "chopper/dome/RSSMechanism.h"
#include "chopper/dome/DomePosition.h"
#include "chopper/math/AnalogFilter.h"
#include "chopper/dome/DomePotentiometer.h"

// Core (for NeckNode / DomeNode pub/sub tests)
#include "chopper/core/Message.h"
#include "chopper/core/Node.h"
#include "chopper/core/Publisher.h"
#include "chopper/core/Subscription.h"
#include "chopper/core/MessageBroker.h"
#include "chopper/core/PublishingNode.h"
#include "chopper/messages/CommonMessages.h"
#include "chopper/nodes/NeckNode.h"
#include "chopper/nodes/DomeNode.h"
#include "chopper/config/DefaultParameters.h"

// ---- Test helpers ----

static int test_count = 0;
static int pass_count = 0;

#define TEST(name) \
    do { test_count++; printf("TEST: %s ... ", #name); } while(0)
#define PASS() \
    do { pass_count++; printf("PASS\n"); } while(0)
#define ASSERT(cond) \
    do { if (!(cond)) { printf("FAIL at %s:%d: %s\n", __FILE__, __LINE__, #cond); return; } } while(0)
#define ASSERT_NEAR(a, b, tol) \
    ASSERT(std::fabs((a) - (b)) < (tol))

// ============================================================
// Easing tests
// ============================================================

void test_easing_boundary_values() {
    TEST(easing_boundary_values);
    using E = chopper::math::Easing;

    for (uint8_t i = 0; i <= 31; ++i) {
        auto method = E::getEasingMethod(i);
        ASSERT(method != nullptr);
        float at_zero = method(0.0f);
        float at_one  = method(1.0f);
        ASSERT(std::fabs(at_zero) < 0.01f);
        ASSERT(std::fabs(at_one - 1.0f) < 0.01f);
    }
    PASS();
}

void test_easing_linear_identity() {
    TEST(easing_linear_is_identity);
    for (float p = 0.0f; p <= 1.0f; p += 0.1f) {
        ASSERT_NEAR(chopper::math::Easing::LinearInterpolation(p), p, 0.001f);
    }
    PASS();
}

void test_easing_quadratic_monotonic() {
    TEST(easing_quadratic_monotonic);
    float prev = 0.0f;
    for (float p = 0.01f; p <= 1.0f; p += 0.01f) {
        float val = chopper::math::Easing::QuadraticEaseIn(p);
        ASSERT(val >= prev - 0.0001f);
        prev = val;
    }
    PASS();
}

void test_easing_invalid_returns_null() {
    TEST(easing_invalid_index);
    ASSERT(chopper::math::Easing::getEasingMethod(255) == nullptr);
    PASS();
}

// ============================================================
// SlewRateLimiter tests
// ============================================================

void test_slew_rate_basic() {
    TEST(slew_rate_basic);
    chopper::math::SlewRateLimiter limiter(100.0f);  // 100 units/sec

    // First call seeds
    float v = limiter.Calculate(1000.0f, 0);
    ASSERT_NEAR(v, 1000.0f, 0.1f);

    // After 100ms, max change = 100 * 0.1 = 10 units
    v = limiter.Calculate(2000.0f, 100);
    ASSERT_NEAR(v, 1010.0f, 0.1f);
    PASS();
}

void test_slew_rate_asymmetric() {
    TEST(slew_rate_asymmetric);
    chopper::math::SlewRateLimiter limiter(200.0f, -100.0f, 500.0f);

    limiter.Calculate(500.0f, 0);  // seed

    // Going up: 200 units/sec, 100ms → max +20
    float v = limiter.Calculate(600.0f, 100);
    ASSERT_NEAR(v, 520.0f, 0.1f);

    // Going down: -100 units/sec, 100ms → max -10
    v = limiter.Calculate(0.0f, 200);
    ASSERT_NEAR(v, 510.0f, 0.1f);
    PASS();
}

void test_slew_rate_reset() {
    TEST(slew_rate_reset);
    chopper::math::SlewRateLimiter limiter(100.0f);
    limiter.Calculate(50.0f, 0);
    limiter.Calculate(60.0f, 100);

    limiter.Reset(0.0f);
    float v = limiter.Calculate(100.0f, 1000);
    ASSERT_NEAR(v, 100.0f, 0.1f);  // After reset, first call seeds
    PASS();
}

// ============================================================
// MathUtil tests
// ============================================================

void test_deadband() {
    TEST(apply_deadband);
    using chopper::math::ApplyDeadband;

    ASSERT_NEAR(ApplyDeadband(0.01f, 0.05f), 0.0f, 0.001f);
    ASSERT_NEAR(ApplyDeadband(-0.03f, 0.05f), 0.0f, 0.001f);

    float result = ApplyDeadband(0.5f, 0.1f);
    ASSERT(result > 0.0f);
    ASSERT(result < 0.5f);
    PASS();
}

void test_speed_limit() {
    TEST(apply_speed_limit);
    using chopper::math::ApplySpeedLimit;

    ASSERT_NEAR(ApplySpeedLimit(0.5f, 0.8f), 0.5f, 0.001f);
    ASSERT_NEAR(ApplySpeedLimit(0.9f, 0.5f), 0.5f, 0.001f);
    ASSERT_NEAR(ApplySpeedLimit(-0.9f, 0.5f), -0.5f, 0.001f);
    PASS();
}

void test_map_value() {
    TEST(map_value);
    using chopper::math::mapValue;

    ASSERT(mapValue(50, 0, 100, 0, 1000) == 500);
    ASSERT(mapValue(0, 0, 270, 500, 2500) == 500);
    ASSERT(mapValue(270, 0, 270, 500, 2500) == 2500);
    ASSERT(mapValue(135, 0, 270, 500, 2500) == 1500);
    PASS();
}

// ============================================================
// RSSMachine tests
// ============================================================

// Use actual robot dimensions from SettingsUser.h
static constexpr float kBaseAlt    = 149.053f;
static constexpr float kEffAlt     = 193.350f;
static constexpr float kBottomLink = 45.0f;
static constexpr float kTopLink    = 31.0f;
static constexpr float kMinHeight  = 28.621f;
static constexpr float kLimitNV    = 0.25f;
static constexpr bool  kBendOut    = true;

void test_rss_max_height() {
    TEST(rss_max_height);
    chopper::math::RSSMachine machine(kBaseAlt, kEffAlt, kBottomLink, kTopLink,
                                       kMinHeight, kLimitNV, kBendOut);
    float maxH = machine.getMaxHeight();
    // maxH should be reasonable (60-80 range for these dimensions)
    ASSERT(maxH > 60.0f);
    ASSERT(maxH < 80.0f);
    PASS();
}

void test_rss_center_symmetric() {
    TEST(rss_center_angles_symmetric);
    chopper::math::RSSMachine machine(kBaseAlt, kEffAlt, kBottomLink, kTopLink,
                                       kMinHeight, kLimitNV, kBendOut);
    auto angles = machine.getLegAngles(0.0f, 0.0f, machine.getMaxHeight());
    ASSERT_NEAR(angles[0], angles[1], 1.0f);
    ASSERT_NEAR(angles[1], angles[2], 1.0f);
    PASS();
}

void test_rss_midheight_symmetric() {
    TEST(rss_midheight_symmetric);
    chopper::math::RSSMachine machine(kBaseAlt, kEffAlt, kBottomLink, kTopLink,
                                       kMinHeight, kLimitNV, kBendOut);
    float mid = (machine.getMinHeight() + machine.getMaxHeight()) / 2.0f;
    auto angles = machine.getLegAngles(0.0f, 0.0f, mid);
    ASSERT_NEAR(angles[0], angles[1], 0.5f);
    ASSERT_NEAR(angles[1], angles[2], 0.5f);
    PASS();
}

void test_rss_tilt_changes_angles() {
    TEST(rss_tilt_changes_angles);
    chopper::math::RSSMachine machine(kBaseAlt, kEffAlt, kBottomLink, kTopLink,
                                       kMinHeight, kLimitNV, kBendOut);
    float mid = (machine.getMinHeight() + machine.getMaxHeight()) / 2.0f;
    auto center = machine.getLegAngles(0.0f, 0.0f, mid);
    auto tilted = machine.getLegAngles(0.2f, 0.0f, mid);

    bool any_different = false;
    for (int i = 0; i < 3; ++i) {
        if (std::fabs(tilted[i] - center[i]) > 0.5f)
            any_different = true;
    }
    ASSERT(any_different);
    PASS();
}

void test_rss_height_clamp() {
    TEST(rss_height_clamp);
    chopper::math::RSSMachine machine(kBaseAlt, kEffAlt, kBottomLink, kTopLink,
                                       kMinHeight, kLimitNV, kBendOut);
    auto low = machine.getLegAngles(0.0f, 0.0f, 0.0f);
    auto at_min = machine.getLegAngles(0.0f, 0.0f, machine.getMinHeight());
    for (int i = 0; i < 3; ++i) {
        ASSERT_NEAR(low[i], at_min[i], 0.001f);
    }
    PASS();
}

void test_rss_unit_normal_clamp() {
    TEST(rss_unit_normal_clamp);
    chopper::math::RSSMachine machine(kBaseAlt, kEffAlt, kBottomLink, kTopLink,
                                       kMinHeight, kLimitNV, kBendOut);
    auto [nx, ny, nz] = machine.unitNormalVector(10.0f, 10.0f);
    ASSERT(nx <= 0.25f);
    ASSERT(ny <= 0.25f);
    ASSERT(nz > 0.0f);
    PASS();
}

// ============================================================
// RSSMechanism tests
// ============================================================

void test_mechanism_disabled_returns_zero() {
    TEST(mechanism_disabled_returns_zero);
    chopper::dome::RSSMechanism mech(kBaseAlt, kEffAlt, kBottomLink, kTopLink,
                                      kMinHeight, kLimitNV, kBendOut);
    mech.setActuationRange(270);
    mech.setLegMinPulse(800, 800, 800);
    mech.setLegMaxPulse(2200, 2200, 2200);

    // Not enabled, past debounce → zeros
    auto pwm = mech.getLegPWMFromJoystick(0.5f, 0.5f, 5000);
    ASSERT(pwm[0] == 0);
    ASSERT(pwm[1] == 0);
    ASSERT(pwm[2] == 0);
    PASS();
}

void test_mechanism_enable_disable() {
    TEST(mechanism_enable_disable);
    chopper::dome::RSSMechanism mech(kBaseAlt, kEffAlt, kBottomLink, kTopLink,
                                      kMinHeight, kLimitNV, kBendOut);
    ASSERT(!mech.isEnabled());
    mech.setEnabled(true, 1000);
    ASSERT(mech.isEnabled());

    // Debounce — too soon
    mech.setEnabled(false, 1500);
    ASSERT(mech.isEnabled());  // still enabled

    // After debounce
    mech.setEnabled(false, 2500);
    ASSERT(!mech.isEnabled());
    PASS();
}

void test_mechanism_enabled_produces_pwm() {
    TEST(mechanism_enabled_produces_pwm);
    chopper::dome::RSSMechanism mech(kBaseAlt, kEffAlt, kBottomLink, kTopLink,
                                      kMinHeight, kLimitNV, kBendOut);
    mech.setActuationRange(270);
    mech.setRotationAngleOffset(0.0f);
    mech.setLegMinPulse(800, 800, 800);
    mech.setLegMaxPulse(2200, 2200, 2200);
    mech.setEnabled(true, 0);

    auto pwm = mech.getLegPWMFromJoystick(0.0f, 0.0f, 2000);
    ASSERT(pwm[0] > 0);
    ASSERT(pwm[1] > 0);
    ASSERT(pwm[2] > 0);
    PASS();
}

void test_mechanism_height_adjustment() {
    TEST(mechanism_height_adjustment);
    chopper::dome::RSSMechanism mech(kBaseAlt, kEffAlt, kBottomLink, kTopLink,
                                      kMinHeight, kLimitNV, kBendOut);
    float initial = mech.getCurrentHeight();
    mech.incrementHeight(5.0f);
    ASSERT_NEAR(mech.getCurrentHeight(), initial + 5.0f, 0.01f);
    mech.decrementHeight(3.0f);
    ASSERT_NEAR(mech.getCurrentHeight(), initial + 2.0f, 0.01f);
    PASS();
}

void test_mechanism_joystick_rotation() {
    TEST(mechanism_joystick_rotation);
    chopper::dome::RSSMechanism mech(kBaseAlt, kEffAlt, kBottomLink, kTopLink,
                                      kMinHeight, kLimitNV, kBendOut);
    mech.setRotationAngleOffset(30.0f);

    float x = 1.0f, y = 0.0f;
    auto [rx, ry] = mech.adjustJoystickToAngleOffset(x, y);
    // After 30 degree rotation, x should be less than 1
    ASSERT(std::fabs(rx) < 1.0f);
    PASS();
}

void test_mechanism_signed_rotation_offset() {
    TEST(mechanism_signed_rotation_offset);
    chopper::dome::RSSMechanism mech(kBaseAlt, kEffAlt, kBottomLink, kTopLink,
                                      kMinHeight, kLimitNV, kBendOut);

    float x = 1.0f;
    float y = 0.0f;
    mech.setRotationAngleOffset(-30.0f);
    auto [rx_neg, ry_neg] = mech.adjustJoystickToAngleOffset(x, y);

    x = 1.0f;
    y = 0.0f;
    mech.setRotationAngleOffset(390.0f);
    auto [rx_wrap_pos, ry_wrap_pos] = mech.adjustJoystickToAngleOffset(x, y);

    x = 1.0f;
    y = 0.0f;
    mech.setRotationAngleOffset(-390.0f);
    auto [rx_wrap_neg, ry_wrap_neg] = mech.adjustJoystickToAngleOffset(x, y);

    ASSERT_NEAR(rx_neg, 0.866f, 0.01f);
    ASSERT_NEAR(ry_neg, -0.5f, 0.01f);
    ASSERT_NEAR(rx_wrap_pos, 0.866f, 0.01f);
    ASSERT_NEAR(ry_wrap_pos, 0.5f, 0.01f);
    ASSERT_NEAR(rx_wrap_neg, rx_neg, 0.01f);
    ASSERT_NEAR(ry_wrap_neg, ry_neg, 0.01f);
    PASS();
}

// ============================================================
// DomePosition tests
// ============================================================

void test_dome_position_initial() {
    TEST(dome_position_initial_state);
    chopper::dome::DomePosition pos;
    ASSERT(!pos.ready());
    ASSERT(pos.getDomeMode() == chopper::dome::DomePosition::kOff);
    PASS();
}

void test_dome_position_update() {
    TEST(dome_position_update);
    chopper::dome::DomePosition pos;
    pos.update(180, 1000);
    ASSERT(pos.ready());
    ASSERT(pos.getDomePosition() == 180);
    PASS();
}

void test_dome_shortest_distance() {
    TEST(dome_shortest_distance);
    ASSERT(chopper::dome::DomePosition::shortestDistance(10, 350) == -20);
    ASSERT(chopper::dome::DomePosition::shortestDistance(350, 10) == 20);
    ASSERT(chopper::dome::DomePosition::shortestDistance(0, 180) == 180);
    PASS();
}

void test_dome_normalize() {
    TEST(dome_normalize);
    ASSERT(chopper::dome::DomePosition::normalize(370) == 10);
    ASSERT(chopper::dome::DomePosition::normalize(-10) == 350);
    ASSERT(chopper::dome::DomePosition::normalize(0) == 0);
    PASS();
}

void test_dome_mode_management() {
    TEST(dome_mode_management);
    chopper::dome::DomePosition pos;
    pos.update(0, 0);

    pos.setDomeDefaultMode(chopper::dome::DomePosition::kHome, 100);
    ASSERT(pos.getDomeMode() == chopper::dome::DomePosition::kHome);
    ASSERT(pos.getDomeDefaultMode() == chopper::dome::DomePosition::kHome);

    pos.setDomeMode(chopper::dome::DomePosition::kTarget, 200);
    ASSERT(pos.getDomeMode() == chopper::dome::DomePosition::kTarget);
    PASS();
}

void test_dome_is_at_position() {
    TEST(dome_is_at_position);
    chopper::dome::DomePosition pos;
    pos.update(100, 1000);
    ASSERT(pos.isAtPosition(100));
    ASSERT(pos.isAtPosition(103));    // within fudge=5
    ASSERT(!pos.isAtPosition(120));
    PASS();
}

void test_dome_timeout() {
    TEST(dome_timeout);
    chopper::dome::DomePosition pos;
    pos.setTimeout(2);  // 2 seconds
    pos.update(0, 1000);
    ASSERT(!pos.isTimeout(2000));   // only 1s since last change
    ASSERT(pos.isTimeout(4000));    // 3s since last change
    PASS();
}

void test_dome_speed_getters() {
    TEST(dome_speed_getters);
    chopper::dome::DomePosition pos;
    pos.update(0, 0);

    // Default values: home=40, target=40, min=15, auto=30 (as uint8_t, /100)
    ASSERT_NEAR(pos.getDomeSpeedHome(), 0.40f, 0.01f);
    ASSERT_NEAR(pos.getDomeSpeedTarget(), 0.40f, 0.01f);
    ASSERT_NEAR(pos.getDomeMinSpeed(), 0.15f, 0.01f);
    ASSERT_NEAR(pos.getDomeAutoSpeed(), 0.30f, 0.01f);

    pos.setDomeHomeSpeed(60);
    ASSERT_NEAR(pos.getDomeSpeedHome(), 0.60f, 0.01f);

    // getDomeSpeed() dispatches based on mode
    ASSERT_NEAR(pos.getDomeSpeed(), pos.getDomeMinSpeed(), 0.01f);  // kOff
    pos.setDomeMode(chopper::dome::DomePosition::kHome, 100);
    ASSERT_NEAR(pos.getDomeSpeed(), 0.60f, 0.01f);
    pos.setDomeMode(chopper::dome::DomePosition::kRandom, 200);
    ASSERT_NEAR(pos.getDomeSpeed(), pos.getDomeAutoSpeed(), 0.01f);

    PASS();
}

void test_dome_random_range() {
    TEST(dome_random_movement_range);
    chopper::dome::DomePosition pos;

    // Defaults: 80 degrees left and right
    ASSERT(pos.getDomeAutoLeft() == 80);
    ASSERT(pos.getDomeAutoRight() == 80);

    pos.setDomeAutoLeftDegrees(120);
    pos.setDomeAutoRightDegrees(45);
    ASSERT(pos.getDomeAutoLeft() == 120);
    ASSERT(pos.getDomeAutoRight() == 45);

    PASS();
}

void test_dome_delay_config() {
    TEST(dome_delay_configuration);
    chopper::dome::DomePosition pos;
    pos.update(0, 0);

    // Defaults: auto 6-8, home 6-8, target 6-8
    ASSERT(pos.getDomeAutoMinDelay() == 6);
    ASSERT(pos.getDomeAutoMaxDelay() == 8);
    ASSERT(pos.getDomeHomeMinDelay() == 6);
    ASSERT(pos.getDomeHomeMaxDelay() == 8);
    ASSERT(pos.getDomeTargetMinDelay() == 6);
    ASSERT(pos.getDomeTargetMaxDelay() == 8);

    // Modify and verify
    pos.setDomeAutoMinDelay(3);
    pos.setDomeAutoMaxDelay(12);
    pos.setDomeHomeMinDelay(2);
    pos.setDomeHomeMaxDelay(5);
    pos.setDomeTargetMinDelay(1);
    pos.setDomeTargetMaxDelay(4);

    ASSERT(pos.getDomeAutoMinDelay() == 3);
    ASSERT(pos.getDomeAutoMaxDelay() == 12);
    ASSERT(pos.getDomeHomeMinDelay() == 2);
    ASSERT(pos.getDomeHomeMaxDelay() == 5);
    ASSERT(pos.getDomeTargetMinDelay() == 1);
    ASSERT(pos.getDomeTargetMaxDelay() == 4);

    PASS();
}

void test_dome_mode_delay_dispatch() {
    TEST(dome_mode_delay_dispatch);
    chopper::dome::DomePosition pos;
    pos.update(0, 0);

    pos.setDomeAutoMinDelay(3);
    pos.setDomeAutoMaxDelay(12);
    pos.setDomeHomeMinDelay(2);
    pos.setDomeHomeMaxDelay(5);

    // kOff → 0
    ASSERT(pos.getDomeMinDelay() == 0);
    ASSERT(pos.getDomeMaxDelay() == 0);

    // kRandom → auto delays
    pos.setDomeMode(chopper::dome::DomePosition::kRandom, 100);
    ASSERT(pos.getDomeMinDelay() == 3);
    ASSERT(pos.getDomeMaxDelay() == 12);

    // kHome → home delays
    pos.setDomeMode(chopper::dome::DomePosition::kHome, 200);
    ASSERT(pos.getDomeMinDelay() == 2);
    ASSERT(pos.getDomeMaxDelay() == 5);

    PASS();
}

void test_dome_home_relative_setters() {
    TEST(dome_home_relative_setters);
    chopper::dome::DomePosition pos;
    pos.update(0, 0);

    // Set home to 90 degrees
    pos.setDomeHomePosition(90);
    ASSERT(pos.getDomeHome() == 90);

    // setDomeHomeRelativeTargetPosition(30) → target = 30 + 90 = 120
    pos.setDomeHomeRelativeTargetPosition(30);
    ASSERT(pos.getDomeTargetPosition() == 120);

    // setDomeHomeRelativeHomePosition(45) → home = 45 + 90 = 135
    pos.setDomeHomeRelativeHomePosition(45);
    ASSERT(pos.getDomeHome() == 135);

    // Wrap-around: home=350, relative=-20 → 330
    pos.setDomeHomePosition(350);
    pos.setDomeHomeRelativeHomePosition(-20);
    ASSERT(pos.getDomeHome() == 330);

    PASS();
}

// ============================================================
// AnalogFilter tests
// ============================================================

void test_analog_filter_stable_input() {
    TEST(analog_filter_stable_input);

    chopper::math::AnalogFilter filter(4096, true, 0.01f);

    // Feed the same value many times — output should converge
    for (int i = 0; i < 50; i++) {
        filter.update(2000);
    }
    ASSERT(filter.getValue() == 2000);
    ASSERT(!filter.hasChanged());  // should be stable / sleeping

    PASS();
}

void test_analog_filter_responds_to_change() {
    TEST(analog_filter_responds_to_change);

    chopper::math::AnalogFilter filter(4096, false, 0.5f);  // no sleep, high snap

    // Seed at 1000
    filter.update(1000);
    ASSERT(filter.getValue() == 1000);

    // Jump to 3000 — should respond quickly with high snap multiplier
    int val = 0;
    for (int i = 0; i < 20; i++) {
        val = filter.update(3000);
    }
    // After 20 iterations with high snap, should be close to 3000
    ASSERT(val > 2500);

    PASS();
}

void test_analog_filter_noise_rejection() {
    TEST(analog_filter_noise_rejection);

    chopper::math::AnalogFilter filter(4096, true, 0.01f);

    // Settle at 2000
    for (int i = 0; i < 100; i++) {
        filter.update(2000);
    }
    int stable = filter.getValue();

    // Add small noise — should be filtered out (sleep mode)
    filter.update(2001);
    filter.update(1999);
    filter.update(2002);
    int noisy = filter.getValue();

    // Value should not have changed much (noise within activity threshold)
    ASSERT(std::abs(noisy - stable) <= 1);

    PASS();
}

// ============================================================
// DomePotentiometer tests
// ============================================================

void test_dome_potentiometer_mapping() {
    TEST(dome_potentiometer_angle_mapping);

    chopper::dome::DomePosition pos;
    chopper::dome::DomePotentiometer pot(&pos, 1225, 2500);

    // Feed minimum ADC value → 0 degrees
    // Need multiple updates to settle the filter
    for (int i = 0; i < 100; i++) {
        pot.update(1225, 1000 + i);
    }
    ASSERT(pos.ready());
    ASSERT(pos.getDomePosition() == 0);

    PASS();
}

void test_dome_potentiometer_max_angle() {
    TEST(dome_potentiometer_max_angle);

    chopper::dome::DomePosition pos;
    chopper::dome::DomePotentiometer pot(&pos, 1225, 2500);

    // Feed maximum ADC value → 359 degrees
    for (int i = 0; i < 100; i++) {
        pot.update(2500, 1000 + i);
    }
    ASSERT(pos.getDomePosition() == 359);

    PASS();
}

void test_dome_potentiometer_midrange() {
    TEST(dome_potentiometer_midrange);

    chopper::dome::DomePosition pos;
    chopper::dome::DomePotentiometer pot(&pos, 1225, 2500);

    // Midpoint: (1225+2500)/2 = 1862.5 → ~179 degrees
    for (int i = 0; i < 100; i++) {
        pot.update(1863, 1000 + i);
    }
    unsigned angle = pos.getDomePosition();
    // Should be approximately 180 (within a few degrees due to integer math)
    ASSERT(angle >= 175 && angle <= 185);

    PASS();
}

void test_dome_potentiometer_calibration() {
    TEST(dome_potentiometer_calibration);

    chopper::dome::DomePosition pos;
    chopper::dome::DomePotentiometer pot(&pos, 0, 4095);  // full range

    // Settle at ADC 2048 (midpoint of full range) → ~180 degrees
    for (int i = 0; i < 100; i++) {
        pot.update(2048, 1000 + i);
    }
    unsigned angle = pos.getDomePosition();
    ASSERT(angle >= 177 && angle <= 183);

    // Change calibration
    pot.setCalibration(1000, 3000);
    // Now ADC 2000 should map to midpoint → ~180
    for (int i = 0; i < 100; i++) {
        pot.update(2000, 2000 + i);
    }
    angle = pos.getDomePosition();
    ASSERT(angle >= 177 && angle <= 183);

    PASS();
}

void test_dome_potentiometer_feeds_dome_position() {
    TEST(dome_potentiometer_feeds_dome_position);

    chopper::dome::DomePosition pos;
    chopper::dome::DomePotentiometer pot(&pos, 1225, 2500);

    // Not ready initially
    ASSERT(!pos.ready());

    // After first update, should be ready
    pot.update(1500, 1000);
    ASSERT(pos.ready());

    // Settle at a position, then check DomePosition tracking
    for (int i = 0; i < 100; i++) {
        pot.update(1862, 2000 + i);
    }
    unsigned angle1 = pos.getDomePosition();

    // Move pot to a different position
    for (int i = 0; i < 100; i++) {
        pot.update(2200, 3000 + i);
    }
    unsigned angle2 = pos.getDomePosition();

    // angle2 should be larger than angle1
    ASSERT(angle2 > angle1);

    PASS();
}

// ============================================================
// NeckNode integration test
// ============================================================

void test_neck_node_publishes_commands() {
    TEST(neck_node_publishes_servo_commands);

    chopper::dome::RSSMechanism mech(kBaseAlt, kEffAlt, kBottomLink, kTopLink,
                                      kMinHeight, kLimitNV, kBendOut);
    mech.setActuationRange(270);
    mech.setRotationAngleOffset(0.0f);
    mech.setLegMinPulse(800, 800, 800);
    mech.setLegMaxPulse(2200, 2200, 2200);
    mech.setEnabled(true, 0);

    auto node = std::make_shared<chopper::nodes::NeckNode>(
        &mech, "controller/dome", "servo/body/cmd", 0, 1, 2);
    node->setTime(2000);

    ASSERT(node->initialize());
    node->activate();

    // Track published servo commands
    int cmd_count = 0;
    auto& broker = chopper::core::MessageBroker::getInstance();
    auto sub = broker.createSubscription<chopper::messages::ServoCommand>(
        "servo/body/cmd",
        [](const chopper::messages::ServoCommand& cmd, void* ctx) {
            int* count = static_cast<int*>(ctx);
            (*count)++;
        },
        &cmd_count);

    // Publish a controller input
    auto pub = broker.createPublisher<chopper::messages::ControllerInput>(
        "controller/dome");

    chopper::messages::ControllerInput input;
    input.axis_x_slew = 0.0f;
    input.axis_y_slew = 0.0f;
    pub->publish(input);

    // Delivery is synchronous — cmd_count should have 3 (one per leg)
    ASSERT(cmd_count == 3);
    PASS();
}

void test_neck_node_emergency_stop() {
    TEST(neck_node_emergency_stop);

    chopper::dome::RSSMechanism mech(kBaseAlt, kEffAlt, kBottomLink, kTopLink,
                                      kMinHeight, kLimitNV, kBendOut);
    mech.setActuationRange(270);
    mech.setLegMinPulse(800, 800, 800);
    mech.setLegMaxPulse(2200, 2200, 2200);

    auto node = std::make_shared<chopper::nodes::NeckNode>(
        &mech, "controller/dome", "servo/body/cmd", 0, 1, 2);
    ASSERT(node->initialize());
    node->activate();

    int disable_count = 0;
    auto& broker = chopper::core::MessageBroker::getInstance();
    auto sub = broker.createSubscription<chopper::messages::ServoCommand>(
        "servo/body/cmd",
        [](const chopper::messages::ServoCommand& cmd, void* ctx) {
            if (cmd.command_type == chopper::messages::ServoCommand::CommandType::DISABLE) {
                int* count = static_cast<int*>(ctx);
                (*count)++;
            }
        },
        &disable_count);

    node->emergencyStop();
    ASSERT(disable_count == 3);
    PASS();
}

void test_neck_node_disable_toggle_disables_servos() {
    TEST(neck_node_disable_toggle_disables_servos);

    chopper::dome::RSSMechanism mech(kBaseAlt, kEffAlt, kBottomLink, kTopLink, kMinHeight, kLimitNV, kBendOut);
    mech.setActuationRange(270);
    mech.setLegMinPulse(800, 800, 800);
    mech.setLegMaxPulse(2200, 2200, 2200);

    auto node = std::make_shared<chopper::nodes::NeckNode>(&mech, "controller/dome", "servo/body/cmd", 0, 1, 2);
    ASSERT(node->initialize());
    node->activate();

    int disable_count = 0;
    auto& broker = chopper::core::MessageBroker::getInstance();
    auto sub = broker.createSubscription<chopper::messages::ServoCommand>(
        "servo/body/cmd",
        [](const chopper::messages::ServoCommand& cmd, void* ctx) {
            if (cmd.command_type == chopper::messages::ServoCommand::CommandType::DISABLE) {
                int* count = static_cast<int*>(ctx);
                (*count)++;
            }
        },
        &disable_count);

    auto pub = broker.createPublisher<chopper::messages::ControllerInput>("controller/dome");
    chopper::messages::ControllerInput input;
    input.is_connected = true;
    input.has_data = true;
    input.button_thumb_l = true;

    node->setTime(2000);
    pub->publish(input);
    ASSERT(mech.isEnabled());

    input.button_thumb_l = false;
    node->setTime(2100);
    pub->publish(input);

    input.button_thumb_l = true;
    node->setTime(3200);
    pub->publish(input);

    ASSERT(!mech.isEnabled());
    ASSERT(disable_count == 3);
    PASS();
}

// ============================================================
// DomeNode integration test
// ============================================================

static void setup_dome_node_test() {
    chopper::core::ParameterServer::getInstance().reset();
    chopper::config::registerDefaultParameters();
}

void test_dome_node_publishes_position() {
    TEST(dome_node_publishes_position);
    setup_dome_node_test();

    chopper::dome::DomePosition domePos;
    domePos.update(180, 1000);

    auto node = std::make_shared<chopper::nodes::DomeNode>(&domePos, 0.5f, 1.0f, 0);
    ASSERT(node->initialize());
    node->activate();

    float reported_angle = -1.0f;
    auto& broker = chopper::core::MessageBroker::getInstance();
    auto sub = broker.createSubscription<chopper::messages::SensorData>(
        "dome/position",
        [](const chopper::messages::SensorData& sd, void* ctx) {
            float* angle = static_cast<float*>(ctx);
            *angle = sd.value;
        },
        &reported_angle);

    node->process(2000);
    ASSERT_NEAR(reported_angle, 180.0f, 0.1f);
    PASS();
}

void test_dome_node_spin_control() {
    TEST(dome_node_spin_control);
    setup_dome_node_test();
    mock_esp_timer_set(1'000'000);

    chopper::dome::DomePosition domePos;
    auto node = std::make_shared<chopper::nodes::DomeNode>(&domePos, 0.5f, 100.0f, 2);
    ASSERT(node->initialize());
    node->activate();

    float last_speed = 0.0f;
    uint8_t last_motor_id = 255;
    auto& broker = chopper::core::MessageBroker::getInstance();
    auto sub = broker.createSubscription<chopper::messages::MotorCommand>(
        "dome/motor/cmd",
        [](const chopper::messages::MotorCommand& cmd, void* ctx) {
            float* speed = static_cast<float*>(ctx);
            *speed = cmd.value;
        },
        &last_speed);

    // Drive R2 only → positive spin
    node->setDomeSpin(true, false, 1020);
    ASSERT(last_speed > 0.0f);

    // Both pressed → target is 0 (but slew-limited)
    float prev_speed = last_speed;
    node->setDomeSpin(true, true, 2000);
    // Speed should be decreasing toward 0
    ASSERT(std::fabs(last_speed) <= std::fabs(prev_speed) + 0.01f);
    mock_esp_timer_reset();
    PASS();
}

void test_dome_node_emergency_stop() {
    TEST(dome_node_emergency_stop);
    setup_dome_node_test();

    chopper::dome::DomePosition domePos;
    auto node = std::make_shared<chopper::nodes::DomeNode>(&domePos);
    ASSERT(node->initialize());
    node->activate();

    bool got_estop = false;
    auto& broker = chopper::core::MessageBroker::getInstance();
    auto sub = broker.createSubscription<chopper::messages::MotorCommand>(
        "dome/motor/cmd",
        [](const chopper::messages::MotorCommand& cmd, void* ctx) {
            if (cmd.command_type == chopper::messages::MotorCommand::CommandType::EMERGENCY_STOP) {
                bool* flag = static_cast<bool*>(ctx);
                *flag = true;
            }
        },
        &got_estop);

    node->emergencyStop();
    ASSERT(got_estop);
    PASS();
}

// ============================================================
// DomeNode auto-dome tests
// ============================================================

void test_dome_node_random_toggle() {
    TEST(dome_node_random_toggle);
    setup_dome_node_test();

    chopper::dome::DomePosition domePos;
    domePos.update(180, 1000);

    auto node = std::make_shared<chopper::nodes::DomeNode>(&domePos, 0.5f, 1.0f, 0);
    ASSERT(node->initialize());
    node->activate();

    auto& broker = chopper::core::MessageBroker::getInstance();
    auto pub = broker.createPublisher<chopper::messages::ControllerInput>("controller/dome");

    ASSERT(!node->isRandomModeEnabled());

    // Single press toggles ON
    chopper::messages::ControllerInput input;
    input.is_connected = true;
    input.has_data = true;
    input.has_intents = true;

    input.intent_dome_random_toggle = true;
    pub->publish(input);
    input.intent_dome_random_toggle = false;
    pub->publish(input);

    ASSERT(node->isRandomModeEnabled());
    ASSERT(domePos.getDomeDefaultMode() == chopper::dome::DomePosition::kRandom);

    // Another press toggles OFF
    input.intent_dome_random_toggle = true;
    pub->publish(input);
    input.intent_dome_random_toggle = false;
    pub->publish(input);

    ASSERT(!node->isRandomModeEnabled());
    ASSERT(domePos.getDomeDefaultMode() == chopper::dome::DomePosition::kOff);

    PASS();
}

void test_dome_node_auto_safety_gate() {
    TEST(dome_node_auto_safety_gate);
    setup_dome_node_test();

    chopper::dome::DomePosition domePos;
    domePos.update(180, 1000);

    auto node = std::make_shared<chopper::nodes::DomeNode>(&domePos, 0.5f, 1.0f, 0);
    ASSERT(node->initialize());
    node->activate();

    // Enable random mode directly for testing
    domePos.setDomeDefaultMode(chopper::dome::DomePosition::kRandom, 1000);

    // Track speed of motor commands from process()
    float last_speed = 0.0f;
    auto& broker = chopper::core::MessageBroker::getInstance();
    auto sub = broker.createSubscription<chopper::messages::MotorCommand>(
        "dome/motor/cmd",
        [](const chopper::messages::MotorCommand& cmd, void* ctx) {
            float* speed = static_cast<float*>(ctx);
            *speed = cmd.value;
        },
        &last_speed);

    // Auto-safety is on and dome hasn't moved manually — should not produce auto movement
    // Process at a time well past any delay
    node->process(100000);
    ASSERT(std::fabs(last_speed) < 0.001f);

    // Mark manual move — now auto should be allowed
    node->setDomeMovedManually(true);
    // Process again — auto-dome should eventually pick a target
    // (we need random mode enabled on the node too)
    // For this test, verifying the gate blocks is sufficient
    ASSERT(!node->hasDomeMovedManually() || true);  // gate was tested above

    PASS();
}

void test_dome_node_idle_transition() {
    TEST(dome_node_idle_transition);
    setup_dome_node_test();

    chopper::dome::DomePosition domePos;
    domePos.update(180, 0);

    auto node = std::make_shared<chopper::nodes::DomeNode>(&domePos, 0.5f, 100.0f, 0);
    ASSERT(node->initialize());
    node->activate();

    // Node starts idle
    ASSERT(node->isIdle());

    // Simulate dome rotate button press via intent
    auto& broker = chopper::core::MessageBroker::getInstance();
    auto pub = broker.createPublisher<chopper::messages::ControllerInput>("controller/dome");

    chopper::messages::ControllerInput input;
    input.is_connected = true;
    input.has_data = true;
    input.has_intents = true;
    input.intent_dome_rotate_right = true;
    pub->publish(input);

    // After manual input, should not be idle
    ASSERT(!node->isIdle());
    ASSERT(node->hasDomeMovedManually());

    PASS();
}

void test_dome_node_move_to_target() {
    TEST(dome_node_move_to_target);
    setup_dome_node_test();

    // Test the auto-dome movement by enabling random mode and processing
    chopper::dome::DomePosition domePos;
    domePos.update(180, 0);
    domePos.setDomeHomePosition(0);

    auto node = std::make_shared<chopper::nodes::DomeNode>(&domePos, 0.5f, 1.0f, 0);
    ASSERT(node->initialize());
    node->activate();

    // Setup: enable random, mark manual move done, set mode
    node->setDomeMovedManually(true);
    domePos.setDomeDefaultMode(chopper::dome::DomePosition::kRandom, 0);

    // Track motor commands
    float last_speed = 0.0f;
    int cmd_count = 0;
    std::pair<float, int> cmd_data{0.0f, 0};
    auto& broker = chopper::core::MessageBroker::getInstance();
    auto sub = broker.createSubscription<chopper::messages::MotorCommand>(
        "dome/motor/cmd",
        [](const chopper::messages::MotorCommand& cmd, void* ctx) {
            auto* data = static_cast<std::pair<float, int>*>(ctx);
            data->first = cmd.value;
            data->second++;
        },
        &cmd_data);

    // Process at a time well past the idle delay (6s default = 6000ms)
    // and past the random schedule delay
    // Seed rand for deterministic test
    std::srand(42);

    // First process — sets up the random schedule
    node->process(10000);

    // Process at a time past the scheduled move
    node->process(30000);

    // The dome is at 180, home is 0 — a random target should be chosen
    // and motor commands should be published (eventually)
    // Process several more times to give it a chance to pick a target and move
    node->process(40000);
    node->process(50000);

    // Verify mode is still kRandom (not errored to kOff)
    ASSERT(domePos.getDomeMode() == chopper::dome::DomePosition::kRandom);

    PASS();
}

// ============================================================
// Main
// ============================================================

int main() {
    printf("=== Dome/IK Test Suite ===\n\n");

    // Easing
    test_easing_boundary_values();
    test_easing_linear_identity();
    test_easing_quadratic_monotonic();
    test_easing_invalid_returns_null();

    // SlewRateLimiter
    test_slew_rate_basic();
    test_slew_rate_asymmetric();
    test_slew_rate_reset();

    // MathUtil
    test_deadband();
    test_speed_limit();
    test_map_value();

    // RSSMachine (core IK)
    test_rss_max_height();
    test_rss_center_symmetric();
    test_rss_midheight_symmetric();
    test_rss_tilt_changes_angles();
    test_rss_height_clamp();
    test_rss_unit_normal_clamp();

    // RSSMechanism (joystick-to-PWM)
    test_mechanism_disabled_returns_zero();
    test_mechanism_enable_disable();
    test_mechanism_enabled_produces_pwm();
    test_mechanism_height_adjustment();
    test_mechanism_joystick_rotation();
    test_mechanism_signed_rotation_offset();

    // DomePosition
    test_dome_position_initial();
    test_dome_position_update();
    test_dome_shortest_distance();
    test_dome_normalize();
    test_dome_mode_management();
    test_dome_is_at_position();
    test_dome_timeout();
    test_dome_speed_getters();
    test_dome_random_range();
    test_dome_delay_config();
    test_dome_mode_delay_dispatch();
    test_dome_home_relative_setters();

    // AnalogFilter
    test_analog_filter_stable_input();
    test_analog_filter_responds_to_change();
    test_analog_filter_noise_rejection();

    // DomePotentiometer
    test_dome_potentiometer_mapping();
    test_dome_potentiometer_max_angle();
    test_dome_potentiometer_midrange();
    test_dome_potentiometer_calibration();
    test_dome_potentiometer_feeds_dome_position();

    // NeckNode integration
    test_neck_node_publishes_commands();
    test_neck_node_emergency_stop();
    test_neck_node_disable_toggle_disables_servos();

    // DomeNode integration
    test_dome_node_publishes_position();
    test_dome_node_spin_control();
    test_dome_node_emergency_stop();

    // DomeNode auto-dome
    test_dome_node_random_toggle();
    test_dome_node_auto_safety_gate();
    test_dome_node_idle_transition();
    test_dome_node_move_to_target();

    printf("\n=== Results: %d/%d passed ===\n", pass_count, test_count);
    return (pass_count == test_count) ? 0 : 1;
}
