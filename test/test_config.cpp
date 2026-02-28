// Host-side tests for configuration system:
//   - HardwareConfig compile-time constants
//   - DefaultParameters registration with ParameterServer
//   - ParameterServer reset, get/set, range validation, change notifications
//
// Compile:
//   g++ -std=c++20 -I test/mocks -I main/include \
//       test/test_config.cpp \
//       main/chopper/core/ParameterServer.cpp \
//       -o test/test_config -pthread

#include <cstdio>
#include <cstring>
#include <cmath>

#include "chopper/config/HardwareConfig.h"
#include "chopper/config/DefaultParameters.h"
#include "chopper/core/ParameterServer.h"

// ---- Test helpers ----

static int test_count = 0;
static int pass_count = 0;

#define TEST(name) \
    do { test_count++; printf("TEST: %s ... ", #name); } while(0)
#define PASS() \
    do { pass_count++; printf("PASS\n"); } while(0)
#define ASSERT(cond) \
    do { if (!(cond)) { printf("FAIL at %s:%d: %s\n", __FILE__, __LINE__, #cond); return; } } while(0)

// ---- Helpers ----

static bool float_eq(float a, float b, float eps = 0.001f) {
    return std::fabs(a - b) < eps;
}

// ---- Test: HardwareConfig constants are accessible ----

void test_hardware_config_pins() {
    TEST(hardware_config_pins);

    using namespace chopper::config::pins;
    ASSERT(SERIAL1_RX == 3);
    ASSERT(SERIAL1_TX == 1);
    ASSERT(SERIAL3_RX == 16);
    ASSERT(SERIAL3_TX == 17);
    ASSERT(SABERTOOTH_TX == SERIAL3_RX);
    ASSERT(MAESTRO_BODY_RX == SERIAL4_RX);
    ASSERT(MAESTRO_BODY_TX == SERIAL4_TX);
    ASSERT(DOME_POTENTIOMETER == DIN34);
    ASSERT(DIN34 == 34);

    PASS();
}

void test_hardware_config_baud() {
    TEST(hardware_config_baud_rates);

    using namespace chopper::config::baud;
    ASSERT(SABERTOOTH == 9600);
    ASSERT(MAESTRO == 9600);
    ASSERT(OPENMV == 115200);
    ASSERT(MP3TRIGGER == 38400);

    PASS();
}

void test_hardware_config_device_ids() {
    TEST(hardware_config_device_ids);

    using namespace chopper::config::device_id;
    ASSERT(SABERTOOTH_TANK_DRIVE == 129);
    ASSERT(SABERTOOTH_DOME_DRIVE == 128);
    ASSERT(MAESTRO_BODY == 12);
    ASSERT(MAESTRO_DOME == 13);

    PASS();
}

void test_hardware_config_servo_channels() {
    TEST(hardware_config_servo_channels);

    using namespace chopper::config::servo_channel;
    ASSERT(BODY_NECK_A == 0);
    ASSERT(BODY_NECK_B == 1);
    ASSERT(BODY_NECK_C == 2);
    ASSERT(BODY_UTILITY_ARM == 3);
    ASSERT(BODY_CHANNEL_COUNT == 6);
    ASSERT(DOME_PERISCOPE_LIFT == 0);
    ASSERT(DOME_PERISCOPE_SPIN == 1);
    ASSERT(DOME_CHANNEL_COUNT == 11);

    PASS();
}

void test_hardware_config_joystick() {
    TEST(hardware_config_joystick);

    using namespace chopper::config::joystick;
    ASSERT(INPUT_MAX == 512);
    ASSERT(INPUT_MIN == -512);
    ASSERT(float_eq(OUTPUT_MAX, 1.0f));
    ASSERT(float_eq(OUTPUT_MIN, -1.0f));

    PASS();
}

void test_hardware_config_bluetooth_macs() {
    TEST(hardware_config_bluetooth_macs);

    using namespace chopper::config::bluetooth;
    ASSERT(strcmp(DRIVE_MAC, "98:E6:B9:62:6E:58") == 0);
    ASSERT(strcmp(DOME_MAC, "98:E6:B9:5A:CA:74") == 0);
    ASSERT(FORGET_ON_STARTUP == true);

    PASS();
}

void test_hardware_config_sound_tracks() {
    TEST(hardware_config_sound_tracks);

    using namespace chopper::config::sound_track;
    ASSERT(GRUMBLY01 == 2);
    ASSERT(MANDOLORIAN == 254);
    ASSERT(IMPERIALCAROLBELLS == 255);

    PASS();
}

void test_hardware_config_drive_modes() {
    TEST(hardware_config_drive_modes);

    using namespace chopper::config::drive_mode;
    ASSERT(ARCADE == 0);
    ASSERT(CURVE == 1);
    ASSERT(TANK == 2);
    ASSERT(REELTWO == 3);

    PASS();
}

// ---- Test: ParameterServer reset ----

void test_parameter_server_reset() {
    TEST(parameter_server_reset);

    auto& ps = chopper::core::ParameterServer::getInstance();
    ps.reset();
    ASSERT(ps.count() == 0);

    ps.declare("test.a", 42);
    ASSERT(ps.count() == 1);

    ps.reset();
    ASSERT(ps.count() == 0);

    // After reset, param should not be found
    int32_t out = 0;
    ASSERT(!ps.get("test.a", out));

    PASS();
}

// ---- Test: DefaultParameters registration ----

void test_default_parameters_registration() {
    TEST(default_parameters_registration);

    auto& ps = chopper::core::ParameterServer::getInstance();
    ps.reset();

    size_t ok = chopper::config::registerDefaultParameters();
    ASSERT(ok == chopper::config::kExpectedParameterCount);
    ASSERT(ps.count() == chopper::config::kExpectedParameterCount);

    PASS();
}

void test_default_parameters_no_double_register() {
    TEST(default_parameters_no_double_register);

    auto& ps = chopper::core::ParameterServer::getInstance();
    ps.reset();

    chopper::config::registerDefaultParameters();
    size_t first_count = ps.count();

    // Second registration should fail (all names already taken)
    size_t ok2 = chopper::config::registerDefaultParameters();
    ASSERT(ok2 == 0);
    ASSERT(ps.count() == first_count);

    PASS();
}

// ---- Test: Safety parameters ----

void test_safety_parameters() {
    TEST(safety_parameters);

    auto& ps = chopper::core::ParameterServer::getInstance();
    ps.reset();
    chopper::config::registerDefaultParameters();

    bool motor_en = false;
    ASSERT(ps.get("safety.motor_enabled", motor_en));
    ASSERT(motor_en == true);

    int32_t timeout = 0;
    ASSERT(ps.get("safety.motor_timeout_ms", timeout));
    ASSERT(timeout == 500);

    int32_t serial_timeout = 0;
    ASSERT(ps.get("safety.serial_timeout_ms", serial_timeout));
    ASSERT(serial_timeout == 450);

    PASS();
}

// ---- Test: Drive parameters ----

void test_drive_parameters() {
    TEST(drive_parameters);

    auto& ps = chopper::core::ParameterServer::getInstance();
    ps.reset();
    chopper::config::registerDefaultParameters();

    float max_speed = 0.0f;
    ASSERT(ps.get("drive.max_speed", max_speed));
    ASSERT(float_eq(max_speed, 0.25f));

    float boost = 0.0f;
    ASSERT(ps.get("drive.speed_boost", boost));
    ASSERT(float_eq(boost, 0.15f));

    float deadband = 0.0f;
    ASSERT(ps.get("drive.deadband", deadband));
    ASSERT(float_eq(deadband, 0.05f));

    bool m1_inv = false;
    ASSERT(ps.get("drive.motor1_inverted", m1_inv));
    ASSERT(m1_inv == true);

    bool m2_inv = true;
    ASSERT(ps.get("drive.motor2_inverted", m2_inv));
    ASSERT(m2_inv == false);

    int32_t sys = 0;
    ASSERT(ps.get("drive.system", sys));
    ASSERT(sys == chopper::config::drive_mode::CURVE);

    PASS();
}

// ---- Test: Dome parameters ----

void test_dome_parameters() {
    TEST(dome_parameters);

    auto& ps = chopper::core::ParameterServer::getInstance();
    ps.reset();
    chopper::config::registerDefaultParameters();

    float max_speed = 0.0f;
    ASSERT(ps.get("dome.max_speed", max_speed));
    ASSERT(float_eq(max_speed, 0.8f));

    float slew = 0.0f;
    ASSERT(ps.get("dome.spin_slew_rate", slew));
    ASSERT(float_eq(slew, 2.0f));

    int32_t thresh = 0;
    ASSERT(ps.get("dome.dir_change_thresh", thresh));
    ASSERT(thresh == 5);

    PASS();
}

// ---- Test: Controller parameters ----

void test_controller_parameters() {
    TEST(controller_parameters);

    auto& ps = chopper::core::ParameterServer::getInstance();
    ps.reset();
    chopper::config::registerDefaultParameters();

    int32_t off_x = 0;
    ASSERT(ps.get("ctrl.drive.offset_x", off_x));
    ASSERT(off_x == -30);

    bool inv_x = false;
    ASSERT(ps.get("ctrl.drive.invert_x", inv_x));
    ASSERT(inv_x == true);

    int32_t dome_off_y = 0;
    ASSERT(ps.get("ctrl.dome.offset_y", dome_off_y));
    ASSERT(dome_off_y == 15);

    bool dome_inv_y = false;
    ASSERT(ps.get("ctrl.dome.invert_y", dome_inv_y));
    ASSERT(dome_inv_y == true);

    float slew = 0.0f;
    ASSERT(ps.get("ctrl.dome.slew_rate", slew));
    ASSERT(float_eq(slew, 3.0f));

    PASS();
}

// ---- Test: RSS mechanism parameters ----

void test_rss_parameters() {
    TEST(rss_parameters);

    auto& ps = chopper::core::ParameterServer::getInstance();
    ps.reset();
    chopper::config::registerDefaultParameters();

    float base_alt = 0.0f;
    ASSERT(ps.get("rss.base_altitude", base_alt));
    ASSERT(float_eq(base_alt, 149.053f));

    float effector = 0.0f;
    ASSERT(ps.get("rss.effector_altitude", effector));
    ASSERT(float_eq(effector, 193.350f));

    float bottom = 0.0f;
    ASSERT(ps.get("rss.bottom_link", bottom));
    ASSERT(float_eq(bottom, 45.0f));

    float top = 0.0f;
    ASSERT(ps.get("rss.top_link", top));
    ASSERT(float_eq(top, 31.0f));

    float min_h = 0.0f;
    ASSERT(ps.get("rss.min_height", min_h));
    ASSERT(float_eq(min_h, 28.621f));

    float limit = 0.0f;
    ASSERT(ps.get("rss.limit_normal", limit));
    ASSERT(float_eq(limit, 0.25f));

    bool bend = false;
    ASSERT(ps.get("rss.bend_out", bend));
    ASSERT(bend == true);

    int32_t act = 0;
    ASSERT(ps.get("rss.actuation_range", act));
    ASSERT(act == 270);

    float rot = 0.0f;
    ASSERT(ps.get("rss.rotation_offset", rot));
    ASSERT(float_eq(rot, -30.0f));

    PASS();
}

// ---- Test: Servo PWM parameters ----

void test_servo_parameters() {
    TEST(servo_parameters);

    auto& ps = chopper::core::ParameterServer::getInstance();
    ps.reset();
    chopper::config::registerDefaultParameters();

    // Neck A
    int32_t neck_a_min = 0;
    ASSERT(ps.get("servo.neck_a.min", neck_a_min));
    ASSERT(neck_a_min == 2032);

    int32_t neck_a_max = 0;
    ASSERT(ps.get("servo.neck_a.max", neck_a_max));
    ASSERT(neck_a_max == 2256);

    bool neck_a_manual = false;
    ASSERT(ps.get("servo.neck_a.manual", neck_a_manual));
    ASSERT(neck_a_manual == true);

    // Periscope lift
    int32_t peri_min = 0;
    ASSERT(ps.get("servo.peri_lift.min", peri_min));
    ASSERT(peri_min == 800);

    int32_t peri_easing = 0;
    ASSERT(ps.get("servo.peri_lift.easing", peri_easing));
    ASSERT(peri_easing == 9);  // CubicEaseInOut

    // Dome door left
    int32_t ddl_neutral = 0;
    ASSERT(ps.get("servo.ddoor_l.neutral", ddl_neutral));
    ASSERT(ddl_neutral == 576);

    PASS();
}

// ---- Test: Parameter set with range validation ----

void test_parameter_set_range_validation() {
    TEST(parameter_set_range_validation);

    auto& ps = chopper::core::ParameterServer::getInstance();
    ps.reset();
    chopper::config::registerDefaultParameters();

    // Set drive.max_speed within range
    ASSERT(ps.set("drive.max_speed", 0.5f));
    float speed = 0.0f;
    ASSERT(ps.get("drive.max_speed", speed));
    ASSERT(float_eq(speed, 0.5f));

    // Set drive.max_speed out of range (> 1.0)
    ASSERT(!ps.set("drive.max_speed", 1.5f));
    ASSERT(ps.get("drive.max_speed", speed));
    ASSERT(float_eq(speed, 0.5f));  // unchanged

    // Set drive.max_speed out of range (< 0.0)
    ASSERT(!ps.set("drive.max_speed", -0.1f));
    ASSERT(ps.get("drive.max_speed", speed));
    ASSERT(float_eq(speed, 0.5f));  // unchanged

    PASS();
}

void test_parameter_set_int_range() {
    TEST(parameter_set_int_range);

    auto& ps = chopper::core::ParameterServer::getInstance();
    ps.reset();
    chopper::config::registerDefaultParameters();

    // Set safety timeout within range
    ASSERT(ps.set("safety.motor_timeout_ms", static_cast<int32_t>(1000)));
    int32_t timeout = 0;
    ASSERT(ps.get("safety.motor_timeout_ms", timeout));
    ASSERT(timeout == 1000);

    // Out of range (> 5000)
    ASSERT(!ps.set("safety.motor_timeout_ms", static_cast<int32_t>(6000)));
    ASSERT(ps.get("safety.motor_timeout_ms", timeout));
    ASSERT(timeout == 1000);  // unchanged

    PASS();
}

// ---- Test: Parameter type mismatch ----

void test_parameter_type_mismatch() {
    TEST(parameter_type_mismatch);

    auto& ps = chopper::core::ParameterServer::getInstance();
    ps.reset();
    chopper::config::registerDefaultParameters();

    // drive.max_speed is float — try to get as int32
    int32_t ival = 0;
    ASSERT(!ps.get("drive.max_speed", ival));

    // safety.motor_enabled is bool — try to get as float
    float fval = 0.0f;
    ASSERT(!ps.get("safety.motor_enabled", fval));

    // safety.motor_timeout_ms is int32 — try to set as float
    ASSERT(!ps.set("safety.motor_timeout_ms", 500.0f));

    PASS();
}

// ---- Test: Parameter change callback ----

static int callback_count = 0;
static char callback_name[64] = {};

static void test_callback(const char* name, void* ctx) {
    callback_count++;
    strncpy(callback_name, name, sizeof(callback_name) - 1);
    if (ctx) {
        (*static_cast<int*>(ctx))++;
    }
}

void test_parameter_change_callback() {
    TEST(parameter_change_callback);

    auto& ps = chopper::core::ParameterServer::getInstance();
    ps.reset();
    chopper::config::registerDefaultParameters();

    callback_count = 0;
    memset(callback_name, 0, sizeof(callback_name));
    int user_ctx = 0;

    ASSERT(ps.onChange("drive.max_speed", test_callback, &user_ctx));

    // Change should trigger callback
    ASSERT(ps.set("drive.max_speed", 0.6f));
    ASSERT(callback_count == 1);
    ASSERT(strcmp(callback_name, "drive.max_speed") == 0);
    ASSERT(user_ctx == 1);

    // Another change
    ASSERT(ps.set("drive.max_speed", 0.3f));
    ASSERT(callback_count == 2);
    ASSERT(user_ctx == 2);

    // Changing a different parameter should not trigger our callback
    ASSERT(ps.set("drive.deadband", 0.1f));
    ASSERT(callback_count == 2);

    PASS();
}

// ---- Test: Nonexistent parameter ----

void test_nonexistent_parameter() {
    TEST(nonexistent_parameter);

    auto& ps = chopper::core::ParameterServer::getInstance();
    ps.reset();
    chopper::config::registerDefaultParameters();

    int32_t ival = 42;
    ASSERT(!ps.get("does.not.exist", ival));
    ASSERT(ival == 42);  // unchanged

    ASSERT(!ps.set("does.not.exist", static_cast<int32_t>(100)));

    PASS();
}

// ---- Test: forEach introspection ----

void test_foreach_introspection() {
    TEST(foreach_introspection);

    auto& ps = chopper::core::ParameterServer::getInstance();
    ps.reset();
    chopper::config::registerDefaultParameters();

    size_t visited = 0;
    ps.forEach([](const chopper::core::Parameter& p, void* ctx) {
        (*static_cast<size_t*>(ctx))++;
        // All parameters should be active
    }, &visited);

    ASSERT(visited == chopper::config::kExpectedParameterCount);

    PASS();
}

// ---- Test: Capacity (88 params fits within MAX_PARAMETERS=128) ----

void test_capacity_within_limits() {
    TEST(capacity_within_limits);

    ASSERT(chopper::config::kExpectedParameterCount <= chopper::limits::MAX_PARAMETERS);
    // Verify there's headroom for user-declared params
    ASSERT(chopper::config::kExpectedParameterCount + 20 <= chopper::limits::MAX_PARAMETERS);

    PASS();
}

// ---- Main ----

int main() {
    printf("\n=== Configuration System Tests ===\n\n");

    // Hardware config (compile-time)
    test_hardware_config_pins();
    test_hardware_config_baud();
    test_hardware_config_device_ids();
    test_hardware_config_servo_channels();
    test_hardware_config_joystick();
    test_hardware_config_bluetooth_macs();
    test_hardware_config_sound_tracks();
    test_hardware_config_drive_modes();

    // ParameterServer reset
    test_parameter_server_reset();

    // DefaultParameters registration
    test_default_parameters_registration();
    test_default_parameters_no_double_register();

    // Parameter value verification
    test_safety_parameters();
    test_drive_parameters();
    test_dome_parameters();
    test_controller_parameters();
    test_rss_parameters();
    test_servo_parameters();

    // ParameterServer behavior
    test_parameter_set_range_validation();
    test_parameter_set_int_range();
    test_parameter_type_mismatch();
    test_parameter_change_callback();
    test_nonexistent_parameter();
    test_foreach_introspection();
    test_capacity_within_limits();

    printf("\n=== Results: %d/%d passed ===\n", pass_count, test_count);
    return (pass_count == test_count) ? 0 : 1;
}
