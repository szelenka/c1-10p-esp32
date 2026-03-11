// Host-side tests for button-to-action nodes and DriveMixer.
//
// Compile:
//   g++ -std=c++20 -I test/mocks -I main/include \
//       test/test_button_nodes.cpp \
//       main/chopper/core/Node.cpp \
//       main/chopper/core/Publisher.cpp \
//       main/chopper/core/Subscription.cpp \
//       main/chopper/core/MessageBroker.cpp \
//       main/chopper/core/PublishingNode.cpp \
//       main/chopper/core/Message.cpp \
//       main/chopper/core/ParameterServer.cpp \
//       -o test/test_button_nodes -pthread

#include <cstdio>
#include <cassert>
#include <cstring>
#include <cmath>
#include <memory>

// Math
#include "chopper/math/DriveMixer.h"
#include "chopper/math/MathUtil.h"

// Core framework
#include "chopper/core/Message.h"
#include "chopper/core/Node.h"
#include "chopper/core/Publisher.h"
#include "chopper/core/Subscription.h"
#include "chopper/core/MessageBroker.h"
#include "chopper/core/PublishingNode.h"
#include "chopper/core/ParameterServer.h"
#include "chopper/messages/CommonMessages.h"
#include "chopper/config/HardwareConfig.h"

// Nodes under test
#include "chopper/nodes/DriveNode.h"
#include "chopper/nodes/PeriscopeNode.h"
#include "chopper/nodes/DomeArmsNode.h"
#include "chopper/nodes/BodyUtilityNode.h"
#include "chopper/nodes/SoundNode.h"

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

/// Reset ParameterServer between tests. MessageBroker auto-cleans
/// via shared_ptr destructors when test-local objects go out of scope.
static void resetFramework() {
    chopper::core::ParameterServer::getInstance().reset();
}

// ============================================================
// DriveMixer tests
// ============================================================

void test_drive_mixer_zero_input() {
    TEST(drive_mixer_zero_input);
    auto [l, r] = chopper::math::ArcadeDriveIK(0.0f, 0.0f);
    ASSERT_NEAR(l, 0.0f, 0.001f);
    ASSERT_NEAR(r, 0.0f, 0.001f);
    PASS();
}

void test_drive_mixer_arcade_symmetry() {
    TEST(drive_mixer_arcade_symmetry);
    // Pure forward: left and right should be equal
    auto [l, r] = chopper::math::ArcadeDriveIK(1.0f, 0.0f);
    ASSERT_NEAR(l, r, 0.001f);
    ASSERT(l > 0.0f);

    // Pure rotation: left and right should be opposite
    auto [l2, r2] = chopper::math::ArcadeDriveIK(0.0f, 0.5f);
    ASSERT_NEAR(l2, -r2, 0.01f);
    PASS();
}

void test_drive_mixer_curvature_turn_in_place() {
    TEST(drive_mixer_curvature_turn_in_place);
    // With allowTurnInPlace=true and xSpeed=0, zRotation drives wheels oppositely
    auto [l, r] = chopper::math::CurvatureDriveIK(0.0f, 0.5f, true);
    ASSERT(l < 0.0f);
    ASSERT(r > 0.0f);

    // Without turn-in-place, zero speed means zero output
    auto [l2, r2] = chopper::math::CurvatureDriveIK(0.0f, 0.5f, false);
    ASSERT_NEAR(l2, 0.0f, 0.001f);
    ASSERT_NEAR(r2, 0.0f, 0.001f);
    PASS();
}

void test_drive_mixer_reeltwo_polar() {
    TEST(drive_mixer_reeltwo_polar);
    // Pure forward (x=1, z=0) should produce both wheels forward
    auto [l, r] = chopper::math::ReelTwoDriveIK(1.0f, 0.0f, false);
    ASSERT(l > 0.0f);
    ASSERT(r > 0.0f);
    ASSERT_NEAR(l, r, 0.01f);
    PASS();
}

void test_drive_mixer_tank_passthrough() {
    TEST(drive_mixer_tank_passthrough);
    // Without squaring, tank drive should pass through directly
    auto [l, r] = chopper::math::TankDriveIK(0.5f, -0.3f, false);
    ASSERT_NEAR(l, 0.5f, 0.001f);
    ASSERT_NEAR(r, -0.3f, 0.001f);
    PASS();
}

void test_drive_mixer_deadband_application() {
    TEST(drive_mixer_deadband_application);
    using chopper::math::ApplyDeadband;
    // Value within deadband returns zero
    ASSERT_NEAR(ApplyDeadband(0.04f, 0.05f), 0.0f, 0.001f);
    // Value outside deadband returns non-zero
    float result = ApplyDeadband(0.5f, 0.05f);
    ASSERT(result > 0.0f);
    ASSERT(result < 0.5f);
    PASS();
}

// ============================================================
// DriveNode tests
// ============================================================

void test_drive_node_forward_publishes_two_motors() {
    TEST(drive_node_forward_publishes_two_motors);
    resetFramework();

    auto node = std::make_shared<chopper::nodes::DriveNode>();
    ASSERT(node->initialize());
    node->activate();

    int motor_count = 0;
    float left_speed = 0.0f;
    float right_speed = 0.0f;

    struct Ctx {
        int* count;
        float* left;
        float* right;
    };
    Ctx ctx{&motor_count, &left_speed, &right_speed};

    auto& broker = chopper::core::MessageBroker::getInstance();
    auto sub = broker.createSubscription<chopper::messages::MotorCommand>(
        "drive/cmd",
        [](const chopper::messages::MotorCommand& cmd, void* c) {
            auto* ctx = static_cast<Ctx*>(c);
            (*ctx->count)++;
            if (cmd.motor_id == 0) *ctx->left = cmd.value;
            if (cmd.motor_id == 1) *ctx->right = cmd.value;
        },
        &ctx);

    auto pub = broker.createPublisher<chopper::messages::ControllerInput>("controller/drive");

    chopper::messages::ControllerInput input;
    input.axis_x_slew = 0.8f;
    input.axis_y_slew = 0.0f;
    pub->publish(input);

    ASSERT(motor_count == 2);
    ASSERT(left_speed > 0.0f);
    ASSERT(right_speed > 0.0f);
    ASSERT_NEAR(left_speed, right_speed, 0.01f);
    PASS();
}

void test_drive_node_turn_asymmetric() {
    TEST(drive_node_turn_asymmetric);
    resetFramework();

    auto node = std::make_shared<chopper::nodes::DriveNode>();
    ASSERT(node->initialize());
    node->activate();

    float left_speed = 0.0f;
    float right_speed = 0.0f;

    struct Ctx { float* left; float* right; };
    Ctx ctx{&left_speed, &right_speed};

    auto& broker = chopper::core::MessageBroker::getInstance();
    auto sub = broker.createSubscription<chopper::messages::MotorCommand>(
        "drive/cmd",
        [](const chopper::messages::MotorCommand& cmd, void* c) {
            auto* ctx = static_cast<Ctx*>(c);
            if (cmd.motor_id == 0) *ctx->left = cmd.value;
            if (cmd.motor_id == 1) *ctx->right = cmd.value;
        },
        &ctx);

    auto pub = broker.createPublisher<chopper::messages::ControllerInput>("controller/drive");

    chopper::messages::ControllerInput input;
    input.axis_x_slew = 0.5f;
    input.axis_y_slew = 0.5f;  // turn
    pub->publish(input);

    // With rotation, left and right should differ
    ASSERT(std::fabs(left_speed - right_speed) > 0.01f);
    PASS();
}

void test_drive_node_zero_publishes_zero() {
    TEST(drive_node_zero_publishes_zero);
    resetFramework();

    auto node = std::make_shared<chopper::nodes::DriveNode>();
    ASSERT(node->initialize());
    node->activate();

    float left_speed = 999.0f;
    float right_speed = 999.0f;

    struct Ctx { float* left; float* right; };
    Ctx ctx{&left_speed, &right_speed};

    auto& broker = chopper::core::MessageBroker::getInstance();
    auto sub = broker.createSubscription<chopper::messages::MotorCommand>(
        "drive/cmd",
        [](const chopper::messages::MotorCommand& cmd, void* c) {
            auto* ctx = static_cast<Ctx*>(c);
            if (cmd.motor_id == 0) *ctx->left = cmd.value;
            if (cmd.motor_id == 1) *ctx->right = cmd.value;
        },
        &ctx);

    auto pub = broker.createPublisher<chopper::messages::ControllerInput>("controller/drive");

    chopper::messages::ControllerInput input;
    input.axis_x_slew = 0.0f;
    input.axis_y_slew = 0.0f;
    pub->publish(input);

    ASSERT_NEAR(left_speed, 0.0f, 0.001f);
    ASSERT_NEAR(right_speed, 0.0f, 0.001f);
    PASS();
}

void test_drive_node_carpet_mode_toggle() {
    TEST(drive_node_carpet_mode_toggle);
    resetFramework();

    auto node = std::make_shared<chopper::nodes::DriveNode>();
    ASSERT(node->initialize());
    node->activate();

    ASSERT(!node->isCarpetMode());

    uint16_t last_track = 0;
    auto& broker = chopper::core::MessageBroker::getInstance();
    auto audio_sub = broker.createSubscription<chopper::messages::AudioCommand>(
        "audio/cmd",
        [](const chopper::messages::AudioCommand& cmd, void* c) {
            uint16_t* track = static_cast<uint16_t*>(c);
            *track = cmd.track_id;
        },
        &last_track);

    // Also subscribe to drive/cmd to consume those messages
    auto motor_sub = broker.createSubscription<chopper::messages::MotorCommand>(
        "drive/cmd",
        [](const chopper::messages::MotorCommand&, void*) {},
        nullptr);

    auto pub = broker.createPublisher<chopper::messages::ControllerInput>("controller/drive");

    // Simulate double-click: press, release, press again quickly
    // The mock esp_timer_get_time returns 0, so both presses have time=0
    // which means (0 - 0) < 500 → double-click detected on second press.

    // First press (rising edge, seeds timestamp)
    chopper::messages::ControllerInput input;
    input.button_thumb_l = true;
    pub->publish(input);

    // Release
    input.button_thumb_l = false;
    pub->publish(input);

    // Second press → double-click detected
    input.button_thumb_l = true;
    pub->publish(input);

    ASSERT(node->isCarpetMode());
    ASSERT(last_track == static_cast<uint16_t>(chopper::config::sound_track::TADA));
    PASS();
}

// ============================================================
// PeriscopeNode tests
// ============================================================

void test_periscope_intent_toggles_lift() {
    TEST(periscope_intent_toggles_lift);
    resetFramework();

    auto node = std::make_shared<chopper::nodes::PeriscopeNode>();
    ASSERT(node->initialize());
    node->activate();
    ASSERT(node->isPeriscopeDown());

    float last_position = 0.0f;
    uint8_t last_servo_id = 255;

    struct Ctx { float* pos; uint8_t* id; };
    Ctx ctx{&last_position, &last_servo_id};

    auto& broker = chopper::core::MessageBroker::getInstance();
    auto sub = broker.createSubscription<chopper::messages::ServoCommand>(
        "servo/dome/cmd",
        [](const chopper::messages::ServoCommand& cmd, void* c) {
            auto* ctx = static_cast<Ctx*>(c);
            *ctx->pos = cmd.value;
            *ctx->id = cmd.servo_id;
        },
        &ctx);

    auto pub = broker.createPublisher<chopper::messages::ControllerInput>("controller/drive");

    // Intent: periscope up — should lift to max
    chopper::messages::ControllerInput input;
    input.has_intents = true;
    input.intent_periscope_up = true;
    pub->publish(input);

    ASSERT(last_servo_id == chopper::config::servo_channel::DOME_PERISCOPE_LIFT);
    ASSERT(!node->isPeriscopeDown());

    int32_t lift_max = 2500;
    chopper::core::ParameterServer::getInstance().get("servo.peri_lift.max", lift_max);
    ASSERT_NEAR(last_position, static_cast<float>(lift_max), 0.1f);

    // Release up intent
    input.intent_periscope_up = false;
    pub->publish(input);

    // Intent: periscope down — should lower to min
    input.intent_periscope_down = true;
    pub->publish(input);

    ASSERT(node->isPeriscopeDown());

    int32_t lift_min = 500;
    chopper::core::ParameterServer::getInstance().get("servo.peri_lift.min", lift_min);
    ASSERT_NEAR(last_position, static_cast<float>(lift_min), 0.1f);
    PASS();
}

void test_periscope_intent_spins_left() {
    TEST(periscope_intent_spins_left);
    resetFramework();

    auto node = std::make_shared<chopper::nodes::PeriscopeNode>();
    ASSERT(node->initialize());
    node->activate();
    ASSERT(node->isPeriscopeDown());
    ASSERT(node->getPeriscopeLocation() == 0);

    float last_position = 0.0f;
    uint8_t last_servo_id = 255;

    struct Ctx { float* pos; uint8_t* id; };
    Ctx ctx{&last_position, &last_servo_id};

    auto& broker = chopper::core::MessageBroker::getInstance();
    auto sub = broker.createSubscription<chopper::messages::ServoCommand>(
        "servo/dome/cmd",
        [](const chopper::messages::ServoCommand& cmd, void* c) {
            auto* ctx = static_cast<Ctx*>(c);
            *ctx->pos = cmd.value;
            *ctx->id = cmd.servo_id;
        },
        &ctx);

    auto pub = broker.createPublisher<chopper::messages::ControllerInput>("controller/drive");

    // Raise the periscope first — spin only works when up
    chopper::messages::ControllerInput input;
    input.has_intents = true;
    input.intent_periscope_up = true;
    pub->publish(input);
    ASSERT(!node->isPeriscopeDown());

    // Release up intent
    input.intent_periscope_up = false;
    pub->publish(input);

    // Spin left from center (→ max)
    input.intent_periscope_spin_left = true;
    pub->publish(input);

    ASSERT(last_servo_id == chopper::config::servo_channel::DOME_PERISCOPE_SPIN);
    ASSERT(node->getPeriscopeLocation() == -1);

    int32_t spin_max = 2500;
    chopper::core::ParameterServer::getInstance().get("servo.peri_spin.max", spin_max);
    ASSERT_NEAR(last_position, static_cast<float>(spin_max), 0.1f);
    PASS();
}

void test_periscope_intent_spins_right() {
    TEST(periscope_intent_spins_right);
    resetFramework();

    auto node = std::make_shared<chopper::nodes::PeriscopeNode>();
    ASSERT(node->initialize());
    node->activate();
    ASSERT(node->isPeriscopeDown());
    ASSERT(node->getPeriscopeLocation() == 0);

    float last_position = 0.0f;
    uint8_t last_servo_id = 255;

    struct Ctx { float* pos; uint8_t* id; };
    Ctx ctx{&last_position, &last_servo_id};

    auto& broker = chopper::core::MessageBroker::getInstance();
    auto sub = broker.createSubscription<chopper::messages::ServoCommand>(
        "servo/dome/cmd",
        [](const chopper::messages::ServoCommand& cmd, void* c) {
            auto* ctx = static_cast<Ctx*>(c);
            *ctx->pos = cmd.value;
            *ctx->id = cmd.servo_id;
        },
        &ctx);

    auto pub = broker.createPublisher<chopper::messages::ControllerInput>("controller/drive");

    // Raise the periscope first — spin only works when up
    chopper::messages::ControllerInput input;
    input.has_intents = true;
    input.intent_periscope_up = true;
    pub->publish(input);
    ASSERT(!node->isPeriscopeDown());

    // Release up intent
    input.intent_periscope_up = false;
    pub->publish(input);

    // Spin right from center (→ min)
    input.intent_periscope_spin_right = true;
    pub->publish(input);

    ASSERT(last_servo_id == chopper::config::servo_channel::DOME_PERISCOPE_SPIN);
    ASSERT(node->getPeriscopeLocation() == 1);

    int32_t spin_min = 500;
    chopper::core::ParameterServer::getInstance().get("servo.peri_spin.min", spin_min);
    ASSERT_NEAR(last_position, static_cast<float>(spin_min), 0.1f);
    PASS();
}

void test_periscope_no_spin_when_down() {
    TEST(periscope_no_spin_when_down);
    resetFramework();

    auto node = std::make_shared<chopper::nodes::PeriscopeNode>();
    ASSERT(node->initialize());
    node->activate();
    ASSERT(node->isPeriscopeDown());

    int cmd_count = 0;
    auto& broker = chopper::core::MessageBroker::getInstance();
    auto sub = broker.createSubscription<chopper::messages::ServoCommand>(
        "servo/dome/cmd",
        [](const chopper::messages::ServoCommand&, void* c) {
            int* count = static_cast<int*>(c);
            (*count)++;
        },
        &cmd_count);

    auto pub = broker.createPublisher<chopper::messages::ControllerInput>("controller/drive");

    // Try spin left while periscope is down — should produce no servo commands
    chopper::messages::ControllerInput input;
    input.has_intents = true;
    input.intent_periscope_spin_left = true;
    pub->publish(input);
    ASSERT(cmd_count == 0);
    PASS();
}

void test_periscope_auto_wander_activates_when_up() {
    TEST(periscope_auto_wander_activates_when_up);
    resetFramework();
    mock_esp_timer_set(1'000'000);  // 1s

    auto node = std::make_shared<chopper::nodes::PeriscopeNode>();
    ASSERT(node->initialize());
    node->activate();
    ASSERT(!node->isAutoWanderActive());

    int cmd_count = 0;
    auto& broker = chopper::core::MessageBroker::getInstance();
    auto sub = broker.createSubscription<chopper::messages::ServoCommand>(
        "servo/dome/cmd",
        [](const chopper::messages::ServoCommand&, void* c) {
            int* count = static_cast<int*>(c);
            (*count)++;
        },
        &cmd_count);

    // process() while down — no auto-wander
    node->process(0);
    ASSERT(!node->isAutoWanderActive());

    // Raise the periscope via intent
    auto pub = broker.createPublisher<chopper::messages::ControllerInput>("controller/drive");
    chopper::messages::ControllerInput input;
    input.has_intents = true;
    input.intent_periscope_up = true;
    pub->publish(input);
    ASSERT(!node->isPeriscopeDown());
    int count_after_lift = cmd_count;

    // First process() when up — activates auto-wander, sets speed, schedules first move
    mock_esp_timer_set(2'000'000);
    node->process(0);
    ASSERT(node->isAutoWanderActive());
    // SET_SPEED command should have been published
    ASSERT(cmd_count > count_after_lift);

    mock_esp_timer_reset();
    PASS();
}

void test_periscope_auto_wander_publishes_position() {
    TEST(periscope_auto_wander_publishes_position);
    resetFramework();
    mock_esp_timer_set(1'000'000);  // 1s

    auto& ps = chopper::core::ParameterServer::getInstance();

    auto node = std::make_shared<chopper::nodes::PeriscopeNode>();
    ASSERT(node->initialize());
    node->activate();

    // Use smallest allowed delays (min=500ms per declared range)
    ps.set("servo.peri_spin.auto_min_delay", static_cast<int32_t>(500));
    ps.set("servo.peri_spin.auto_max_delay", static_cast<int32_t>(1000));

    float last_position = -1.0f;
    uint8_t last_servo_id = 255;
    uint8_t last_cmd_type = 255;

    struct Ctx { float* pos; uint8_t* id; uint8_t* type; };
    Ctx ctx{&last_position, &last_servo_id, &last_cmd_type};

    auto& broker = chopper::core::MessageBroker::getInstance();
    auto sub = broker.createSubscription<chopper::messages::ServoCommand>(
        "servo/dome/cmd",
        [](const chopper::messages::ServoCommand& cmd, void* c) {
            auto* ctx = static_cast<Ctx*>(c);
            *ctx->pos = cmd.value;
            *ctx->id = cmd.servo_id;
            *ctx->type = static_cast<uint8_t>(cmd.command_type);
        },
        &ctx);

    // Raise periscope
    auto pub = broker.createPublisher<chopper::messages::ControllerInput>("controller/drive");
    chopper::messages::ControllerInput input;
    input.has_intents = true;
    input.intent_periscope_up = true;
    pub->publish(input);
    ASSERT(!node->isPeriscopeDown());

    // First process — activates, schedules move at now + [500,1000)ms
    mock_esp_timer_set(2'000'000);  // 2s
    node->process(0);
    ASSERT(node->isAutoWanderActive());

    // Advance well past max delay — should publish a SET_POSITION to spin channel
    mock_esp_timer_set(4'000'000);  // 4s (+2s, well past 1s max delay)
    node->process(0);

    ASSERT(last_servo_id == chopper::config::servo_channel::DOME_PERISCOPE_SPIN);
    ASSERT(last_cmd_type == static_cast<uint8_t>(chopper::messages::ServoCommand::CommandType::SET_POSITION));

    // Target should be within spin range
    int32_t spin_min = 500, spin_max = 2500;
    ps.get("servo.peri_spin.min", spin_min);
    ps.get("servo.peri_spin.max", spin_max);
    ASSERT(last_position >= static_cast<float>(spin_min));
    ASSERT(last_position <= static_cast<float>(spin_max));

    mock_esp_timer_reset();
    PASS();
}

void test_periscope_auto_wander_stops_when_lowered() {
    TEST(periscope_auto_wander_stops_when_lowered);
    resetFramework();
    mock_esp_timer_set(1'000'000);

    auto node = std::make_shared<chopper::nodes::PeriscopeNode>();
    ASSERT(node->initialize());
    node->activate();

    auto& broker = chopper::core::MessageBroker::getInstance();
    auto pub = broker.createPublisher<chopper::messages::ControllerInput>("controller/drive");

    // Raise
    chopper::messages::ControllerInput input;
    input.has_intents = true;
    input.intent_periscope_up = true;
    pub->publish(input);
    ASSERT(!node->isPeriscopeDown());

    // Start auto-wander
    mock_esp_timer_set(2'000'000);
    node->process(0);
    ASSERT(node->isAutoWanderActive());

    // Lower periscope
    mock_esp_timer_set(3'000'000);
    input.intent_periscope_up = false;
    pub->publish(input);
    input.intent_periscope_down = true;
    pub->publish(input);
    ASSERT(node->isPeriscopeDown());

    // process() should deactivate auto-wander
    mock_esp_timer_set(4'000'000);
    node->process(0);
    ASSERT(!node->isAutoWanderActive());

    mock_esp_timer_reset();
    PASS();
}

void test_periscope_manual_spin_resets_auto_wander() {
    TEST(periscope_manual_spin_resets_auto_wander);
    resetFramework();
    mock_esp_timer_set(1'000'000);  // 1s

    auto& ps = chopper::core::ParameterServer::getInstance();

    auto node = std::make_shared<chopper::nodes::PeriscopeNode>();
    ASSERT(node->initialize());
    node->activate();

    // Use smallest allowed delays for faster test
    ps.set("servo.peri_spin.auto_min_delay", static_cast<int32_t>(500));
    ps.set("servo.peri_spin.auto_max_delay", static_cast<int32_t>(1000));

    auto& broker = chopper::core::MessageBroker::getInstance();
    auto pub = broker.createPublisher<chopper::messages::ControllerInput>("controller/drive");

    // Raise
    chopper::messages::ControllerInput input;
    input.has_intents = true;
    input.intent_periscope_up = true;
    pub->publish(input);

    // Start auto-wander
    mock_esp_timer_set(2'000'000);  // 2s
    node->process(0);
    ASSERT(node->isAutoWanderActive());

    // Manual spin left — should reset auto-wander
    mock_esp_timer_set(3'000'000);  // 3s
    input.intent_periscope_up = false;
    pub->publish(input);
    input.intent_periscope_spin_left = true;
    pub->publish(input);
    ASSERT(!node->isAutoWanderActive());

    // Auto-wander should not reactivate until cooldown (auto_max_delay=1000ms) has passed
    mock_esp_timer_set(3'500'000);  // 3.5s — only 500ms after manual input
    input.intent_periscope_spin_left = false;
    pub->publish(input);
    node->process(0);
    ASSERT(!node->isAutoWanderActive());

    // After cooldown passes, auto-wander reactivates
    mock_esp_timer_set(5'000'000);  // 5s — 2s after manual input, well past 1s cooldown
    node->process(0);
    ASSERT(node->isAutoWanderActive());

    mock_esp_timer_reset();
    PASS();
}

// ============================================================
// DomeArmsNode tests
// ============================================================

void test_dome_arms_toggle_doors() {
    TEST(dome_arms_toggle_doors);
    resetFramework();

    auto node = std::make_shared<chopper::nodes::DomeArmsNode>();
    ASSERT(node->initialize());
    node->activate();

    // Initial state: doors are open
    ASSERT(node->isRightDoorOpen());
    ASSERT(node->isLeftDoorOpen());

    int cmd_count = 0;
    auto& broker = chopper::core::MessageBroker::getInstance();
    auto sub = broker.createSubscription<chopper::messages::ServoCommand>(
        "servo/dome/cmd",
        [](const chopper::messages::ServoCommand& cmd, void* c) {
            int* count = static_cast<int*>(c);
            (*count)++;
        },
        &cmd_count);

    auto pub = broker.createPublisher<chopper::messages::ControllerInput>("controller/drive");

    // Press miscSelect: should toggle both doors (close them)
    chopper::messages::ControllerInput input;
    input.misc_select = true;
    pub->publish(input);

    ASSERT(cmd_count == 2);  // one for each door
    ASSERT(!node->isRightDoorOpen());
    ASSERT(!node->isLeftDoorOpen());
    PASS();
}

void test_dome_arms_second_press_reverses() {
    TEST(dome_arms_second_press_reverses);
    resetFramework();

    auto node = std::make_shared<chopper::nodes::DomeArmsNode>();
    ASSERT(node->initialize());
    node->activate();

    auto& broker = chopper::core::MessageBroker::getInstance();
    auto sub = broker.createSubscription<chopper::messages::ServoCommand>(
        "servo/dome/cmd",
        [](const chopper::messages::ServoCommand&, void*) {},
        nullptr);

    auto pub = broker.createPublisher<chopper::messages::ControllerInput>("controller/drive");

    // First press: close doors
    chopper::messages::ControllerInput input;
    input.misc_select = true;
    pub->publish(input);
    ASSERT(!node->isRightDoorOpen());
    ASSERT(!node->isLeftDoorOpen());

    // Release
    input.misc_select = false;
    pub->publish(input);

    // Second press: re-open doors
    input.misc_select = true;
    pub->publish(input);
    ASSERT(node->isRightDoorOpen());
    ASSERT(node->isLeftDoorOpen());
    PASS();
}

// ============================================================
// BodyUtilityNode tests
// ============================================================

void test_body_utility_b_extends() {
    TEST(body_utility_b_extends);
    resetFramework();

    auto node = std::make_shared<chopper::nodes::BodyUtilityNode>();
    ASSERT(node->initialize());
    node->activate();

    float last_position = 0.0f;
    uint8_t last_servo_id = 255;

    struct Ctx { float* pos; uint8_t* id; };
    Ctx ctx{&last_position, &last_servo_id};

    auto& broker = chopper::core::MessageBroker::getInstance();
    auto sub = broker.createSubscription<chopper::messages::ServoCommand>(
        "servo/body/cmd",
        [](const chopper::messages::ServoCommand& cmd, void* c) {
            auto* ctx = static_cast<Ctx*>(c);
            *ctx->pos = cmd.value;
            *ctx->id = cmd.servo_id;
        },
        &ctx);

    auto pub = broker.createPublisher<chopper::messages::ControllerInput>("controller/drive");

    // Press B: extends arm to max
    chopper::messages::ControllerInput input;
    input.button_b = true;
    pub->publish(input);

    ASSERT(last_servo_id == chopper::config::servo_channel::BODY_UTILITY_ARM);
    int32_t max_pos = 2500;
    chopper::core::ParameterServer::getInstance().get("servo.util_arm.max", max_pos);
    ASSERT_NEAR(last_position, static_cast<float>(max_pos), 0.1f);
    PASS();
}

void test_body_utility_b_retracts_on_release() {
    TEST(body_utility_b_retracts_on_release);
    resetFramework();

    auto node = std::make_shared<chopper::nodes::BodyUtilityNode>();
    ASSERT(node->initialize());
    node->activate();

    float last_position = 0.0f;
    auto& broker = chopper::core::MessageBroker::getInstance();
    auto sub = broker.createSubscription<chopper::messages::ServoCommand>(
        "servo/body/cmd",
        [](const chopper::messages::ServoCommand& cmd, void* c) {
            float* pos = static_cast<float*>(c);
            *pos = cmd.value;
        },
        &last_position);

    auto pub = broker.createPublisher<chopper::messages::ControllerInput>("controller/drive");

    // Press B
    chopper::messages::ControllerInput input;
    input.button_b = true;
    pub->publish(input);

    // Release B: retracts to neutral
    input.button_b = false;
    pub->publish(input);

    int32_t neutral = 1500;
    chopper::core::ParameterServer::getInstance().get("servo.util_arm.neutral", neutral);
    ASSERT_NEAR(last_position, static_cast<float>(neutral), 0.1f);
    PASS();
}

// ============================================================
// SoundNode tests
// ============================================================

void test_sound_a_plays_correct_track() {
    TEST(sound_a_plays_correct_track);
    resetFramework();

    auto node = std::make_shared<chopper::nodes::SoundNode>();
    ASSERT(node->initialize());
    node->activate();

    uint16_t last_track = 0;
    auto& broker = chopper::core::MessageBroker::getInstance();
    auto sub = broker.createSubscription<chopper::messages::AudioCommand>(
        "audio/cmd",
        [](const chopper::messages::AudioCommand& cmd, void* c) {
            uint16_t* track = static_cast<uint16_t*>(c);
            *track = cmd.track_id;
        },
        &last_track);

    auto pub = broker.createPublisher<chopper::messages::ControllerInput>("controller/dome");

    chopper::messages::ControllerInput input;
    input.button_a = true;
    pub->publish(input);

    ASSERT(last_track == static_cast<uint16_t>(chopper::config::sound_track::IMPERIALCAROLBELLS));
    PASS();
}

void test_sound_b_plays_correct_track() {
    TEST(sound_b_plays_correct_track);
    resetFramework();

    auto node = std::make_shared<chopper::nodes::SoundNode>();
    ASSERT(node->initialize());
    node->activate();

    uint16_t last_track = 0;
    auto& broker = chopper::core::MessageBroker::getInstance();
    auto sub = broker.createSubscription<chopper::messages::AudioCommand>(
        "audio/cmd",
        [](const chopper::messages::AudioCommand& cmd, void* c) {
            uint16_t* track = static_cast<uint16_t*>(c);
            *track = cmd.track_id;
        },
        &last_track);

    auto pub = broker.createPublisher<chopper::messages::ControllerInput>("controller/dome");

    chopper::messages::ControllerInput input;
    input.button_b = true;
    pub->publish(input);

    ASSERT(last_track == static_cast<uint16_t>(chopper::config::sound_track::MANDOLORIAN));
    PASS();
}

void test_sound_misc_start_plays_track() {
    TEST(sound_misc_start_plays_track);
    resetFramework();

    auto node = std::make_shared<chopper::nodes::SoundNode>();
    ASSERT(node->initialize());
    node->activate();

    uint16_t last_track = 0;
    bool got_audio = false;

    struct Ctx { uint16_t* track; bool* got; };
    Ctx ctx{&last_track, &got_audio};

    auto& broker = chopper::core::MessageBroker::getInstance();
    auto sub = broker.createSubscription<chopper::messages::AudioCommand>(
        "audio/cmd",
        [](const chopper::messages::AudioCommand& cmd, void* c) {
            auto* ctx = static_cast<Ctx*>(c);
            *ctx->track = cmd.track_id;
            *ctx->got = true;
        },
        &ctx);

    auto pub = broker.createPublisher<chopper::messages::ControllerInput>("controller/dome");

    chopper::messages::ControllerInput input;
    input.misc_start = true;
    pub->publish(input);

    ASSERT(got_audio);
    ASSERT(last_track > 0);
    PASS();
}

// ============================================================
// Intent-path tests (has_intents = true)
// ============================================================

void test_dome_arms_intent_toggles_doors() {
    TEST(dome_arms_intent_toggles_doors);
    resetFramework();

    auto node = std::make_shared<chopper::nodes::DomeArmsNode>();
    ASSERT(node->initialize());
    node->activate();
    ASSERT(node->isRightDoorOpen());

    auto& broker = chopper::core::MessageBroker::getInstance();
    auto sub = broker.createSubscription<chopper::messages::ServoCommand>(
        "servo/dome/cmd",
        [](const chopper::messages::ServoCommand&, void*) {},
        nullptr);

    auto pub = broker.createPublisher<chopper::messages::ControllerInput>("controller/drive");

    // With intents, misc_select is ignored; intent_dome_doors_toggle is used
    chopper::messages::ControllerInput input;
    input.has_intents = true;
    input.intent_dome_doors_toggle = true;
    input.misc_select = false;  // raw button not pressed
    pub->publish(input);

    ASSERT(!node->isRightDoorOpen());
    PASS();
}

void test_body_utility_intent_extends() {
    TEST(body_utility_intent_extends);
    resetFramework();

    auto node = std::make_shared<chopper::nodes::BodyUtilityNode>();
    ASSERT(node->initialize());
    node->activate();

    float last_position = 0.0f;
    auto& broker = chopper::core::MessageBroker::getInstance();
    auto sub = broker.createSubscription<chopper::messages::ServoCommand>(
        "servo/body/cmd",
        [](const chopper::messages::ServoCommand& cmd, void* c) {
            float* pos = static_cast<float*>(c);
            *pos = cmd.value;
        },
        &last_position);

    auto pub = broker.createPublisher<chopper::messages::ControllerInput>("controller/drive");

    // With intents, button_b is ignored; intent_body_utility_toggle is used
    chopper::messages::ControllerInput input;
    input.has_intents = true;
    input.intent_body_utility_toggle = true;
    input.button_b = false;
    pub->publish(input);

    int32_t max_pos = 2500;
    chopper::core::ParameterServer::getInstance().get("servo.util_arm.max", max_pos);
    ASSERT_NEAR(last_position, static_cast<float>(max_pos), 0.1f);
    PASS();
}

void test_drive_node_carpet_mode_via_intent() {
    TEST(drive_node_carpet_mode_via_intent);
    resetFramework();

    auto node = std::make_shared<chopper::nodes::DriveNode>();
    ASSERT(node->initialize());
    node->activate();
    ASSERT(!node->isCarpetMode());

    auto& broker = chopper::core::MessageBroker::getInstance();
    auto motor_sub = broker.createSubscription<chopper::messages::MotorCommand>(
        "drive/cmd",
        [](const chopper::messages::MotorCommand&, void*) {},
        nullptr);
    auto audio_sub = broker.createSubscription<chopper::messages::AudioCommand>(
        "audio/cmd",
        [](const chopper::messages::AudioCommand&, void*) {},
        nullptr);

    auto pub = broker.createPublisher<chopper::messages::ControllerInput>("controller/drive");

    // Double-click via intent (not raw button)
    chopper::messages::ControllerInput input;
    input.has_intents = true;
    input.intent_carpet_mode_toggle = true;
    input.button_thumb_l = false;
    pub->publish(input);

    input.intent_carpet_mode_toggle = false;
    pub->publish(input);

    input.intent_carpet_mode_toggle = true;
    pub->publish(input);

    ASSERT(node->isCarpetMode());
    PASS();
}

void test_sound_a_via_intent() {
    TEST(sound_a_via_intent);
    resetFramework();

    auto node = std::make_shared<chopper::nodes::SoundNode>();
    ASSERT(node->initialize());
    node->activate();

    uint16_t last_track = 0;
    auto& broker = chopper::core::MessageBroker::getInstance();
    auto sub = broker.createSubscription<chopper::messages::AudioCommand>(
        "audio/cmd",
        [](const chopper::messages::AudioCommand& cmd, void* c) {
            uint16_t* track = static_cast<uint16_t*>(c);
            *track = cmd.track_id;
        },
        &last_track);

    auto pub = broker.createPublisher<chopper::messages::ControllerInput>("controller/dome");

    // With intents, button_a is ignored; intent_sound_a is used
    chopper::messages::ControllerInput input;
    input.has_intents = true;
    input.intent_sound_a = true;
    input.button_a = false;
    pub->publish(input);

    ASSERT(last_track == static_cast<uint16_t>(chopper::config::sound_track::IMPERIALCAROLBELLS));
    PASS();
}

// ============================================================
// Main
// ============================================================

int main() {
    printf("=== Button Nodes Test Suite ===\n\n");

    // DriveMixer
    test_drive_mixer_zero_input();
    test_drive_mixer_arcade_symmetry();
    test_drive_mixer_curvature_turn_in_place();
    test_drive_mixer_reeltwo_polar();
    test_drive_mixer_tank_passthrough();
    test_drive_mixer_deadband_application();

    // DriveNode
    test_drive_node_forward_publishes_two_motors();
    test_drive_node_turn_asymmetric();
    test_drive_node_zero_publishes_zero();
    test_drive_node_carpet_mode_toggle();

    // PeriscopeNode
    test_periscope_intent_toggles_lift();
    test_periscope_intent_spins_left();
    test_periscope_intent_spins_right();
    test_periscope_no_spin_when_down();
    test_periscope_auto_wander_activates_when_up();
    test_periscope_auto_wander_publishes_position();
    test_periscope_auto_wander_stops_when_lowered();
    test_periscope_manual_spin_resets_auto_wander();

    // DomeArmsNode
    test_dome_arms_toggle_doors();
    test_dome_arms_second_press_reverses();

    // BodyUtilityNode
    test_body_utility_b_extends();
    test_body_utility_b_retracts_on_release();

    // SoundNode
    test_sound_a_plays_correct_track();
    test_sound_b_plays_correct_track();
    test_sound_misc_start_plays_track();

    // Intent-path tests
    test_dome_arms_intent_toggles_doors();
    test_body_utility_intent_extends();
    test_drive_node_carpet_mode_via_intent();
    test_sound_a_via_intent();

    printf("\n=== Results: %d/%d passed ===\n", pass_count, test_count);
    return (pass_count == test_count) ? 0 : 1;
}
