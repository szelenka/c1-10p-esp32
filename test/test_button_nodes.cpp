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
#include <algorithm>

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
#include "chopper/config/DefaultParameters.h"
#include "chopper/config/HardwareConfig.h"

// Nodes under test
#include "chopper/nodes/DomeNode.h"
#include "chopper/nodes/DriveNode.h"
#include "chopper/nodes/PeriscopeNode.h"
#include "chopper/nodes/DomeArmsNode.h"
#include "chopper/nodes/BodyDoorsNode.h"
#include "chopper/nodes/BodyLedNode.h"
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

/// Reset ParameterServer and register all default parameters.
/// DefaultParameters.h is the single source of truth for all param values.
static void resetFramework() {
    chopper::core::ParameterServer::getInstance().reset();
    chopper::config::registerDefaultParameters();
}

static chopper::messages::ControllerInput connectedControllerInput() {
    chopper::messages::ControllerInput input;
    input.is_connected = true;
    input.has_data = true;
    return input;
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
    mock_esp_timer_set(1'000'000);

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

    mock_esp_timer_set(1'020'000);
    auto input = connectedControllerInput();
    input.axis_x_slew = 0.8f;
    input.axis_y_slew = 0.0f;
    pub->publish(input);

    ASSERT(motor_count == 2);
    ASSERT(left_speed > 0.0f);
    ASSERT(right_speed > 0.0f);
    ASSERT_NEAR(left_speed, right_speed, 0.01f);
    ASSERT(left_speed < 0.05f);
    ASSERT(right_speed < 0.05f);
    mock_esp_timer_reset();
    PASS();
}

void test_drive_node_turn_asymmetric() {
    TEST(drive_node_turn_asymmetric);
    resetFramework();
    mock_esp_timer_set(1'000'000);

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

    mock_esp_timer_set(1'100'000);
    auto input = connectedControllerInput();
    input.axis_x_slew = 0.5f;
    input.axis_y_slew = 0.5f;  // turn
    pub->publish(input);

    // With rotation, left and right should differ
    ASSERT(std::fabs(left_speed - right_speed) > 0.01f);
    mock_esp_timer_reset();
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

    auto input = connectedControllerInput();
    input.axis_x_slew = 0.0f;
    input.axis_y_slew = 0.0f;
    pub->publish(input);

    ASSERT_NEAR(left_speed, 0.0f, 0.001f);
    ASSERT_NEAR(right_speed, 0.0f, 0.001f);
    PASS();
}

void test_drive_node_disconnect_zero_bypasses_slew() {
    TEST(drive_node_disconnect_zero_bypasses_slew);
    resetFramework();
    mock_esp_timer_set(1'000'000);

    auto node = std::make_shared<chopper::nodes::DriveNode>();
    ASSERT(node->initialize());
    node->activate();

    float left_speed = 0.0f;
    float right_speed = 0.0f;

    struct Ctx {
        float* left;
        float* right;
    };
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

    auto input = connectedControllerInput();
    input.axis_x_slew = 1.0f;
    mock_esp_timer_set(1'500'000);
    pub->publish(input);
    ASSERT(left_speed > 0.0f);
    ASSERT(right_speed > 0.0f);

    mock_esp_timer_set(1'520'000);
    chopper::messages::ControllerInput zero;
    zero.has_intents = true;
    zero.is_connected = false;
    zero.has_data = false;
    pub->publish(zero);

    ASSERT_NEAR(left_speed, 0.0f, 0.001f);
    ASSERT_NEAR(right_speed, 0.0f, 0.001f);
    mock_esp_timer_reset();
    PASS();
}

void test_drive_node_connected_neutral_zero_bypasses_slew() {
    TEST(drive_node_connected_neutral_zero_bypasses_slew);
    resetFramework();
    mock_esp_timer_set(1'000'000);

    auto node = std::make_shared<chopper::nodes::DriveNode>();
    ASSERT(node->initialize());
    node->activate();

    float left_speed = 0.0f;
    float right_speed = 0.0f;

    struct Ctx {
        float* left;
        float* right;
    };
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

    auto input = connectedControllerInput();
    input.axis_x_slew = 1.0f;
    mock_esp_timer_set(1'500'000);
    pub->publish(input);
    ASSERT(left_speed > 0.0f);
    ASSERT(right_speed > 0.0f);

    input.axis_x_slew = 0.0f;
    input.axis_y_slew = 0.0f;
    input.axis_x_normalized = 0.0f;
    input.axis_y_normalized = 0.0f;
    mock_esp_timer_set(1'520'000);
    pub->publish(input);

    ASSERT_NEAR(left_speed, 0.0f, 0.001f);
    ASSERT_NEAR(right_speed, 0.0f, 0.001f);
    mock_esp_timer_reset();
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
    input.is_connected = true;
    input.has_data = true;
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

void test_drive_node_transient_carpet_mode_active_intent() {
    TEST(drive_node_transient_carpet_mode_active_intent);
    resetFramework();
    mock_esp_timer_set(1'000'000);

    auto node = std::make_shared<chopper::nodes::DriveNode>();
    ASSERT(node->initialize());
    node->activate();
    ASSERT(!node->isCarpetMode());

    int motor_count = 0;
    float left_speed = 0.0f;
    float right_speed = 0.0f;
    int audio_count = 0;

    struct MotorCtx {
        int* count;
        float* left;
        float* right;
    };
    MotorCtx motor_ctx{&motor_count, &left_speed, &right_speed};

    auto& broker = chopper::core::MessageBroker::getInstance();
    auto motor_sub = broker.createSubscription<chopper::messages::MotorCommand>(
        "drive/cmd",
        [](const chopper::messages::MotorCommand& cmd, void* c) {
            auto* ctx = static_cast<MotorCtx*>(c);
            (*ctx->count)++;
            if (cmd.motor_id == 0) *ctx->left = cmd.value;
            if (cmd.motor_id == 1) *ctx->right = cmd.value;
        },
        &motor_ctx);
    auto audio_sub = broker.createSubscription<chopper::messages::AudioCommand>(
        "audio/cmd",
        [](const chopper::messages::AudioCommand&, void* c) { (*static_cast<int*>(c))++; },
        &audio_count);

    auto pub = broker.createPublisher<chopper::messages::ControllerInput>("controller/drive");

    float max_speed = 0.0f;
    float speed_boost = 0.0f;
    chopper::core::ParameterServer::getInstance().get("drive.max_speed", max_speed);
    chopper::core::ParameterServer::getInstance().get("drive.speed_boost", speed_boost);
    const float boosted_max = std::clamp(max_speed + speed_boost, 0.0f, 1.0f);

    auto input = connectedControllerInput();
    input.has_intents = true;
    input.intent_carpet_mode_active = true;
    input.axis_x_slew = 1.0f;
    input.axis_x_normalized = 1.0f;
    mock_esp_timer_set(2'000'000);
    pub->publish(input);

    ASSERT(motor_count == 2);
    ASSERT_NEAR(left_speed, boosted_max, 0.01f);
    ASSERT_NEAR(right_speed, boosted_max, 0.01f);
    ASSERT(!node->isCarpetMode());
    ASSERT(audio_count == 0);

    input.intent_carpet_mode_active = false;
    input.axis_x_slew = 0.0f;
    input.axis_x_normalized = 0.0f;
    mock_esp_timer_set(2'020'000);
    pub->publish(input);

    ASSERT_NEAR(left_speed, 0.0f, 0.001f);
    ASSERT_NEAR(right_speed, 0.0f, 0.001f);

    input.axis_x_slew = 1.0f;
    input.axis_x_normalized = 1.0f;
    mock_esp_timer_set(3'020'000);
    pub->publish(input);

    ASSERT_NEAR(left_speed, max_speed, 0.01f);
    ASSERT_NEAR(right_speed, max_speed, 0.01f);
    ASSERT(!node->isCarpetMode());
    ASSERT(audio_count == 0);
    mock_esp_timer_reset();
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
    uint16_t last_duration = 0;
    bool last_has_start = false;

    struct Ctx {
        float* pos;
        uint8_t* id;
        uint16_t* duration;
        bool* has_start;
    };
    Ctx ctx{&last_position, &last_servo_id, &last_duration, &last_has_start};

    auto& broker = chopper::core::MessageBroker::getInstance();
    auto sub = broker.createSubscription<chopper::messages::ServoCommand>(
        "servo/dome/move",
        [](const chopper::messages::ServoCommand& cmd, void* c) {
            auto* ctx = static_cast<Ctx*>(c);
            *ctx->pos = cmd.value;
            *ctx->id = cmd.servo_id;
            *ctx->duration = cmd.duration_ms;
            *ctx->has_start = cmd.has_start_value;
        },
        &ctx);

    auto pub = broker.createPublisher<chopper::messages::ControllerInput>("controller/drive");

    // Intent: periscope up — should lift to max
    auto input = connectedControllerInput();
    input.has_intents = true;
    input.intent_periscope_up = true;
    pub->publish(input);

    ASSERT(last_servo_id == chopper::config::servo_channel::DOME_PERISCOPE_LIFT);
    ASSERT(!node->isPeriscopeDown());

    int32_t lift_max = 2500;
    chopper::core::ParameterServer::getInstance().get("servo.peri_lift.max", lift_max);
    ASSERT_NEAR(last_position, static_cast<float>(lift_max), 0.1f);
    ASSERT(last_duration == 800);
    ASSERT(last_has_start);

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
    ASSERT(last_duration == 800);
    ASSERT(last_has_start);
    PASS();
}

void test_periscope_led_colors_only_when_lifted() {
    TEST(periscope_led_colors_only_when_lifted);
    resetFramework();
    mock_esp_timer_set(1'000'000);

    auto node = std::make_shared<chopper::nodes::PeriscopeNode>();
    ASSERT(node->initialize());
    node->activate();

    int periscope_led_count = 0;
    chopper::messages::LEDCommand last_led;
    struct LedCtx {
        chopper::messages::LEDCommand* last;
        int* count;
    };
    LedCtx led_ctx{&last_led, &periscope_led_count};

    auto& broker = chopper::core::MessageBroker::getInstance();
    auto led_sub = broker.createSubscription<chopper::messages::LEDCommand>(
        "led/dome_eye/cmd",
        [](const chopper::messages::LEDCommand& cmd, void* c) {
            if (cmd.led_id != 4) {
                return;
            }
            auto* ctx = static_cast<LedCtx*>(c);
            (*ctx->count)++;
            *ctx->last = cmd;
        },
        &led_ctx);
    ASSERT(led_sub != nullptr);

    auto led_pub = broker.createPublisher<chopper::messages::LEDCommand>("led/dome_eye/cmd");
    ASSERT(led_pub != nullptr);
    chopper::messages::LEDCommand eye_color;
    eye_color.command_type = chopper::messages::LEDCommand::CommandType::SET_COLOR;
    eye_color.led_id = 1;
    eye_color.color.red = 255;
    led_pub->publish(eye_color);
    ASSERT(periscope_led_count == 0);

    auto ctrl_pub = broker.createPublisher<chopper::messages::ControllerInput>("controller/drive");

    auto input = connectedControllerInput();
    input.has_intents = true;
    input.intent_periscope_up = true;
    ctrl_pub->publish(input);

    ASSERT(periscope_led_count == 0);

    mock_esp_timer_set(1'799'000);
    node->process(1'799'000);
    ASSERT(periscope_led_count == 0);

    mock_esp_timer_set(1'800'000);
    node->process(1'800'000);
    ASSERT(periscope_led_count == 1);
    ASSERT(last_led.command_type == chopper::messages::LEDCommand::CommandType::SET_COLOR);
    ASSERT(last_led.led_id == 4);
    ASSERT(last_led.color.red == 255);

    input.intent_periscope_up = false;
    ctrl_pub->publish(input);

    mock_esp_timer_set(1'820'000);
    input.intent_periscope_down = true;
    ctrl_pub->publish(input);

    ASSERT(periscope_led_count == 2);
    ASSERT(last_led.command_type == chopper::messages::LEDCommand::CommandType::TURN_OFF);
    ASSERT(last_led.led_id == 4);
    mock_esp_timer_reset();
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
    uint16_t last_duration = 0;
    bool last_has_start = false;

    struct Ctx {
        float* pos;
        uint8_t* id;
        uint16_t* duration;
        bool* has_start;
    };
    Ctx ctx{&last_position, &last_servo_id, &last_duration, &last_has_start};

    auto& broker = chopper::core::MessageBroker::getInstance();
    auto sub = broker.createSubscription<chopper::messages::ServoCommand>(
        "servo/dome/move",
        [](const chopper::messages::ServoCommand& cmd, void* c) {
            auto* ctx = static_cast<Ctx*>(c);
            *ctx->pos = cmd.value;
            *ctx->id = cmd.servo_id;
            *ctx->duration = cmd.duration_ms;
            *ctx->has_start = cmd.has_start_value;
        },
        &ctx);

    auto pub = broker.createPublisher<chopper::messages::ControllerInput>("controller/drive");

    // Raise the periscope first — spin only works when up
    auto input = connectedControllerInput();
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
    ASSERT(last_duration == 400);
    ASSERT(last_has_start);
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
    uint16_t last_duration = 0;
    bool last_has_start = false;

    struct Ctx {
        float* pos;
        uint8_t* id;
        uint16_t* duration;
        bool* has_start;
    };
    Ctx ctx{&last_position, &last_servo_id, &last_duration, &last_has_start};

    auto& broker = chopper::core::MessageBroker::getInstance();
    auto sub = broker.createSubscription<chopper::messages::ServoCommand>(
        "servo/dome/move",
        [](const chopper::messages::ServoCommand& cmd, void* c) {
            auto* ctx = static_cast<Ctx*>(c);
            *ctx->pos = cmd.value;
            *ctx->id = cmd.servo_id;
            *ctx->duration = cmd.duration_ms;
            *ctx->has_start = cmd.has_start_value;
        },
        &ctx);

    auto pub = broker.createPublisher<chopper::messages::ControllerInput>("controller/drive");

    // Raise the periscope first — spin only works when up
    auto input = connectedControllerInput();
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
    ASSERT(last_duration == 400);
    ASSERT(last_has_start);
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
        "servo/dome/move",
        [](const chopper::messages::ServoCommand&, void* c) {
            int* count = static_cast<int*>(c);
            (*count)++;
        },
        &cmd_count);

    auto pub = broker.createPublisher<chopper::messages::ControllerInput>("controller/drive");

    // Try spin left while periscope is down — should produce no servo commands
    auto input = connectedControllerInput();
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
        "servo/dome/move",
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
    auto input = connectedControllerInput();
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
        "servo/dome/move",
        [](const chopper::messages::ServoCommand& cmd, void* c) {
            auto* ctx = static_cast<Ctx*>(c);
            *ctx->pos = cmd.value;
            *ctx->id = cmd.servo_id;
            *ctx->type = static_cast<uint8_t>(cmd.command_type);
        },
        &ctx);

    // Raise periscope
    auto pub = broker.createPublisher<chopper::messages::ControllerInput>("controller/drive");
    auto input = connectedControllerInput();
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
    auto input = connectedControllerInput();
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
    auto input = connectedControllerInput();
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
    float right_value = 0.0f;
    float left_value = 0.0f;
    uint16_t right_duration = 0;
    uint16_t left_duration = 0;
    bool right_has_start = false;
    bool left_has_start = false;
    struct Ctx {
        int* count;
        float* right;
        float* left;
        uint16_t* right_duration;
        uint16_t* left_duration;
        bool* right_has_start;
        bool* left_has_start;
    };
    Ctx ctx{&cmd_count,        &right_value,        &left_value,        &right_duration,
            &left_duration,    &right_has_start,    &left_has_start};

    auto& broker = chopper::core::MessageBroker::getInstance();
    auto sub = broker.createSubscription<chopper::messages::ServoCommand>(
        "servo/dome/move",
        [](const chopper::messages::ServoCommand& cmd, void* c) {
            auto* ctx = static_cast<Ctx*>(c);
            (*ctx->count)++;
            if (cmd.servo_id == chopper::config::servo_channel::DOME_DOOR_RIGHT) {
                *ctx->right = cmd.value;
                *ctx->right_duration = cmd.duration_ms;
                *ctx->right_has_start = cmd.has_start_value;
            }
            if (cmd.servo_id == chopper::config::servo_channel::DOME_DOOR_LEFT) {
                *ctx->left = cmd.value;
                *ctx->left_duration = cmd.duration_ms;
                *ctx->left_has_start = cmd.has_start_value;
            }
        },
        &ctx);

    auto pub = broker.createPublisher<chopper::messages::ControllerInput>("controller/drive");

    // Press miscSelect: should toggle both doors (close them)
    chopper::messages::ControllerInput input;
    input.misc_select = true;
    pub->publish(input);

    ASSERT(cmd_count == 2);  // one for each door
    ASSERT(!node->isRightDoorOpen());
    ASSERT(!node->isLeftDoorOpen());

    int32_t rdoor_min = 0;
    int32_t ldoor_max = 0;
    chopper::core::ParameterServer::getInstance().get("servo.ddoor_r.min", rdoor_min);
    chopper::core::ParameterServer::getInstance().get("servo.ddoor_l.max", ldoor_max);
    ASSERT_NEAR(right_value, static_cast<float>(rdoor_min), 0.1f);
    ASSERT_NEAR(left_value, static_cast<float>(ldoor_max), 0.1f);
    ASSERT(right_duration == 1);
    ASSERT(left_duration == 1);
    ASSERT(right_has_start);
    ASSERT(left_has_start);
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
        "servo/dome/move",
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
    uint16_t last_duration = 0;
    bool last_has_start = false;

    struct Ctx {
        float* pos;
        uint8_t* id;
        uint16_t* duration;
        bool* has_start;
    };
    Ctx ctx{&last_position, &last_servo_id, &last_duration, &last_has_start};

    auto& broker = chopper::core::MessageBroker::getInstance();
    auto sub = broker.createSubscription<chopper::messages::ServoCommand>(
        "servo/body/move",
        [](const chopper::messages::ServoCommand& cmd, void* c) {
            auto* ctx = static_cast<Ctx*>(c);
            *ctx->pos = cmd.value;
            *ctx->id = cmd.servo_id;
            *ctx->duration = cmd.duration_ms;
            *ctx->has_start = cmd.has_start_value;
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
    ASSERT(last_duration == 800);
    ASSERT(last_has_start);
    PASS();
}

void test_body_utility_b_retracts_on_release() {
    TEST(body_utility_b_retracts_on_release);
    resetFramework();

    auto node = std::make_shared<chopper::nodes::BodyUtilityNode>();
    ASSERT(node->initialize());
    node->activate();

    float last_position = 0.0f;
    uint16_t last_duration = 0;
    bool last_has_start = false;
    struct Ctx {
        float* pos;
        uint16_t* duration;
        bool* has_start;
    };
    Ctx ctx{&last_position, &last_duration, &last_has_start};

    auto& broker = chopper::core::MessageBroker::getInstance();
    auto sub = broker.createSubscription<chopper::messages::ServoCommand>(
        "servo/body/move",
        [](const chopper::messages::ServoCommand& cmd, void* c) {
            auto* ctx = static_cast<Ctx*>(c);
            *ctx->pos = cmd.value;
            *ctx->duration = cmd.duration_ms;
            *ctx->has_start = cmd.has_start_value;
        },
        &ctx);

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
    ASSERT(last_duration == 800);
    ASSERT(last_has_start);
    PASS();
}

void test_body_utility_intent_toggles_open_closed() {
    TEST(body_utility_intent_toggles_open_closed);
    resetFramework();

    auto node = std::make_shared<chopper::nodes::BodyUtilityNode>();
    ASSERT(node->initialize());
    node->activate();

    float last_position = 0.0f;
    auto& broker = chopper::core::MessageBroker::getInstance();
    auto sub = broker.createSubscription<chopper::messages::ServoCommand>(
        "servo/body/move",
        [](const chopper::messages::ServoCommand& cmd, void* c) {
            float* pos = static_cast<float*>(c);
            *pos = cmd.value;
        },
        &last_position);

    auto pub = broker.createPublisher<chopper::messages::ControllerInput>("controller/drive");

    auto input = connectedControllerInput();
    input.has_intents = true;
    input.intent_body_utility_toggle = true;
    pub->publish(input);

    int32_t max_pos = 2500;
    chopper::core::ParameterServer::getInstance().get("servo.util_arm.max", max_pos);
    ASSERT_NEAR(last_position, static_cast<float>(max_pos), 0.1f);

    input.intent_body_utility_toggle = false;
    pub->publish(input);
    input.intent_body_utility_toggle = true;
    pub->publish(input);

    int32_t neutral = 1500;
    chopper::core::ParameterServer::getInstance().get("servo.util_arm.neutral", neutral);
    ASSERT_NEAR(last_position, static_cast<float>(neutral), 0.1f);
    PASS();
}

void test_body_doors_intents_toggle_individual_doors() {
    TEST(body_doors_intents_toggle_individual_doors);
    resetFramework();

    auto node = std::make_shared<chopper::nodes::BodyDoorsNode>();
    ASSERT(node->initialize());
    node->activate();

    float last_position = 0.0f;
    uint8_t last_servo_id = 255;
    struct Ctx {
        float* pos;
        uint8_t* id;
    };
    Ctx ctx{&last_position, &last_servo_id};

    auto& broker = chopper::core::MessageBroker::getInstance();
    auto sub = broker.createSubscription<chopper::messages::ServoCommand>(
        "servo/body/move",
        [](const chopper::messages::ServoCommand& cmd, void* c) {
            auto* ctx = static_cast<Ctx*>(c);
            *ctx->pos = cmd.value;
            *ctx->id = cmd.servo_id;
        },
        &ctx);

    auto pub = broker.createPublisher<chopper::messages::ControllerInput>("controller/drive");

    auto input = connectedControllerInput();
    input.has_intents = true;
    input.intent_body_left_door_toggle = true;
    pub->publish(input);

    int32_t left_open = 1696;
    chopper::core::ParameterServer::getInstance().get("servo.bdoor_l.max", left_open);
    ASSERT(node->isLeftDoorOpen());
    ASSERT(last_servo_id == chopper::config::servo_channel::BODY_DOOR_LEFT);
    ASSERT_NEAR(last_position, static_cast<float>(left_open), 0.1f);

    input.intent_body_left_door_toggle = false;
    pub->publish(input);
    input.intent_body_right_door_toggle = true;
    pub->publish(input);

    int32_t right_open = 992;
    chopper::core::ParameterServer::getInstance().get("servo.bdoor_r.min", right_open);
    ASSERT(node->isRightDoorOpen());
    ASSERT(last_servo_id == chopper::config::servo_channel::BODY_DOOR_RIGHT);
    ASSERT_NEAR(last_position, static_cast<float>(right_open), 0.1f);
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

void test_sound_drive_sl_sr_changes_volume() {
    TEST(sound_drive_sl_sr_changes_volume);
    resetFramework();

    auto node = std::make_shared<chopper::nodes::SoundNode>();
    ASSERT(node->initialize());
    node->activate();

    uint8_t last_volume = 255;
    uint8_t last_type = 255;
    int command_count = 0;

    struct Ctx {
        uint8_t* volume;
        uint8_t* type;
        int* count;
    };
    Ctx ctx{&last_volume, &last_type, &command_count};

    auto& broker = chopper::core::MessageBroker::getInstance();
    auto sub = broker.createSubscription<chopper::messages::AudioCommand>(
        "audio/cmd",
        [](const chopper::messages::AudioCommand& cmd, void* c) {
            auto* ctx = static_cast<Ctx*>(c);
            *ctx->volume = cmd.volume;
            *ctx->type = static_cast<uint8_t>(cmd.command_type);
            (*ctx->count)++;
        },
        &ctx);

    auto pub = broker.createPublisher<chopper::messages::ControllerInput>("controller/drive");

    chopper::messages::ControllerInput input;
    input.button_r1 = true;  // SR: volume down, MP3 value increases.
    pub->publish(input);

    ASSERT(command_count == 1);
    ASSERT(last_type == static_cast<uint8_t>(chopper::messages::AudioCommand::CommandType::SET_VOLUME));
    ASSERT(last_volume ==
           chopper::config::sound::DEFAULT_VOLUME + chopper::config::sound::VOLUME_STEP);

    input.button_r1 = false;
    pub->publish(input);
    ASSERT(command_count == 1);

    input.button_l1 = true;  // SL: volume up, MP3 value decreases.
    pub->publish(input);

    ASSERT(command_count == 2);
    ASSERT(last_type == static_cast<uint8_t>(chopper::messages::AudioCommand::CommandType::SET_VOLUME));
    ASSERT(last_volume == chopper::config::sound::DEFAULT_VOLUME);
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
        "servo/dome/move",
        [](const chopper::messages::ServoCommand&, void*) {},
        nullptr);

    auto pub = broker.createPublisher<chopper::messages::ControllerInput>("controller/drive");

    // With intents, misc_select is ignored; intent_dome_doors_toggle is used
    auto input = connectedControllerInput();
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
        "servo/body/move",
        [](const chopper::messages::ServoCommand& cmd, void* c) {
            float* pos = static_cast<float*>(c);
            *pos = cmd.value;
        },
        &last_position);

    auto pub = broker.createPublisher<chopper::messages::ControllerInput>("controller/drive");

    // With intents, button_b is ignored; intent_body_utility_toggle is used
    auto input = connectedControllerInput();
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
    auto input = connectedControllerInput();
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
    auto input = connectedControllerInput();
    input.has_intents = true;
    input.intent_sound_a = true;
    input.button_a = false;
    pub->publish(input);

    ASSERT(last_track == static_cast<uint16_t>(chopper::config::sound_track::IMPERIALCAROLBELLS));
    PASS();
}

void test_sound_volume_via_drive_intent() {
    TEST(sound_volume_via_drive_intent);
    resetFramework();

    auto node = std::make_shared<chopper::nodes::SoundNode>();
    ASSERT(node->initialize());
    node->activate();

    uint8_t last_volume = 255;
    uint8_t last_type = 255;

    struct Ctx {
        uint8_t* volume;
        uint8_t* type;
    };
    Ctx ctx{&last_volume, &last_type};

    auto& broker = chopper::core::MessageBroker::getInstance();
    auto sub = broker.createSubscription<chopper::messages::AudioCommand>(
        "audio/cmd",
        [](const chopper::messages::AudioCommand& cmd, void* c) {
            auto* ctx = static_cast<Ctx*>(c);
            *ctx->volume = cmd.volume;
            *ctx->type = static_cast<uint8_t>(cmd.command_type);
        },
        &ctx);

    auto pub = broker.createPublisher<chopper::messages::ControllerInput>("controller/drive");

    auto input = connectedControllerInput();
    input.has_intents = true;
    input.intent_volume_down = true;
    input.button_r1 = false;
    pub->publish(input);

    ASSERT(last_type == static_cast<uint8_t>(chopper::messages::AudioCommand::CommandType::SET_VOLUME));
    ASSERT(last_volume ==
           chopper::config::sound::DEFAULT_VOLUME + chopper::config::sound::VOLUME_STEP);
    PASS();
}

// ============================================================
// DomeNode button rotation tests
// ============================================================

void test_dome_drive_l2_publishes_positive_speed() {
    TEST(dome_drive_l2_publishes_positive_speed);
    resetFramework();
    mock_esp_timer_set(1'000'000);

    auto node = std::make_shared<chopper::nodes::DomeNode>(nullptr, 0.5f, 1.0f, 2, false, 320);
    ASSERT(node->initialize());

    auto& broker = chopper::core::MessageBroker::getInstance();
    auto drive_pub = broker.createPublisher<chopper::messages::ControllerInput>("controller/drive");

    float last_speed = 0.0f;
    int motor_count = 0;
    struct Ctx {
        float* speed;
        int* count;
    };
    Ctx ctx{&last_speed, &motor_count};
    auto motor_sub = broker.createSubscription<chopper::messages::MotorCommand>(
        "dome/motor/cmd",
        [](const chopper::messages::MotorCommand& cmd, void* c) {
            auto* x = static_cast<Ctx*>(c);
            (*x->count)++;
            *x->speed = cmd.value;
        },
        &ctx);

    // Drive controller L2 pressed → dome rotate left → positive speed
    auto input = connectedControllerInput();
    input.has_intents = true;
    input.intent_dome_rotate_left = true;
    mock_esp_timer_set(1'500'000);
    drive_pub->publish(input);
    // Motor commands are published during process(), not in the input handler
    node->process(1'500'000);

    ASSERT(motor_count > 0);
    float dome_max_speed = 0.5f;
    chopper::core::ParameterServer::getInstance().get("dome.max_speed", dome_max_speed);
    ASSERT_NEAR(last_speed, dome_max_speed, 0.01f);
    mock_esp_timer_reset();
    PASS();
}

void test_dome_dome_l2_publishes_negative_speed() {
    TEST(dome_dome_l2_publishes_negative_speed);
    resetFramework();
    mock_esp_timer_set(1'000'000);

    auto node = std::make_shared<chopper::nodes::DomeNode>(nullptr, 0.5f, 1.0f, 2, false, 320);
    ASSERT(node->initialize());

    auto& broker = chopper::core::MessageBroker::getInstance();
    auto dome_pub = broker.createPublisher<chopper::messages::ControllerInput>("controller/dome");

    float last_speed = 0.0f;
    int motor_count = 0;
    struct Ctx {
        float* speed;
        int* count;
    };
    Ctx ctx{&last_speed, &motor_count};
    auto motor_sub = broker.createSubscription<chopper::messages::MotorCommand>(
        "dome/motor/cmd",
        [](const chopper::messages::MotorCommand& cmd, void* c) {
            auto* x = static_cast<Ctx*>(c);
            (*x->count)++;
            *x->speed = cmd.value;
        },
        &ctx);

    // Dome controller L2 pressed → dome rotate right → negative speed
    auto input = connectedControllerInput();
    input.has_intents = true;
    input.intent_dome_rotate_right = true;
    mock_esp_timer_set(1'500'000);
    dome_pub->publish(input);
    node->process(1'500'000);

    ASSERT(motor_count > 0);
    float dome_max_speed = 0.5f;
    chopper::core::ParameterServer::getInstance().get("dome.max_speed", dome_max_speed);
    ASSERT_NEAR(last_speed, -dome_max_speed, 0.01f);
    mock_esp_timer_reset();
    PASS();
}

void test_dome_no_button_publishes_zero() {
    TEST(dome_no_button_publishes_zero);
    resetFramework();

    auto node = std::make_shared<chopper::nodes::DomeNode>(nullptr, 0.5f, 1.0f, 2, false, 320);
    ASSERT(node->initialize());

    auto& broker = chopper::core::MessageBroker::getInstance();
    auto dome_pub = broker.createPublisher<chopper::messages::ControllerInput>("controller/dome");

    float last_speed = -999.0f;
    int motor_count = 0;
    struct Ctx {
        float* speed;
        int* count;
    };
    Ctx ctx{&last_speed, &motor_count};
    auto motor_sub = broker.createSubscription<chopper::messages::MotorCommand>(
        "dome/motor/cmd",
        [](const chopper::messages::MotorCommand& cmd, void* c) {
            auto* x = static_cast<Ctx*>(c);
            (*x->count)++;
            *x->speed = cmd.value;
        },
        &ctx);

    // No buttons pressed → should still publish with zero speed
    auto input = connectedControllerInput();
    input.has_intents = true;
    dome_pub->publish(input);
    node->process(1000);

    ASSERT(motor_count > 0);
    ASSERT_NEAR(last_speed, 0.0f, 0.01f);
    PASS();
}

void test_dome_analog_rx_publishes_proportional_speed() {
    TEST(dome_analog_rx_publishes_proportional_speed);
    resetFramework();
    mock_esp_timer_set(1'000'000);

    auto node = std::make_shared<chopper::nodes::DomeNode>(nullptr, 0.5f, 100.0f, 2, false, 320);
    ASSERT(node->initialize());

    auto& broker = chopper::core::MessageBroker::getInstance();
    auto dome_pub = broker.createPublisher<chopper::messages::ControllerInput>("controller/dome");

    float last_speed = 0.0f;
    int motor_count = 0;
    struct Ctx {
        float* speed;
        int* count;
    };
    Ctx ctx{&last_speed, &motor_count};
    auto motor_sub = broker.createSubscription<chopper::messages::MotorCommand>(
        "dome/motor/cmd",
        [](const chopper::messages::MotorCommand& cmd, void* c) {
            auto* x = static_cast<Ctx*>(c);
            (*x->count)++;
            *x->speed = cmd.value;
        },
        &ctx);

    auto input = connectedControllerInput();
    input.has_intents = true;
    input.axis_rx_normalized = 0.5f;
    mock_esp_timer_set(1'500'000);
    dome_pub->publish(input);
    node->process(1'500'000);

    ASSERT(motor_count > 0);
    float dome_max_speed = 0.5f;
    chopper::core::ParameterServer::getInstance().get("dome.max_speed", dome_max_speed);
    const float expected = -dome_max_speed * chopper::math::ApplyDeadband(0.5f, 0.05f);
    ASSERT_NEAR(last_speed, expected, 0.01f);
    mock_esp_timer_reset();
    PASS();
}

void test_dome_first_manual_command_slews_from_zero() {
    TEST(dome_first_manual_command_slews_from_zero);
    resetFramework();
    mock_esp_timer_set(1'000'000);

    auto node = std::make_shared<chopper::nodes::DomeNode>(nullptr, 0.5f, 1.0f, 2, false, 320);
    ASSERT(node->initialize());

    auto& broker = chopper::core::MessageBroker::getInstance();
    auto dome_pub = broker.createPublisher<chopper::messages::ControllerInput>("controller/dome");

    float last_speed = 0.0f;
    int motor_count = 0;
    struct Ctx {
        float* speed;
        int* count;
    };
    Ctx ctx{&last_speed, &motor_count};
    auto motor_sub = broker.createSubscription<chopper::messages::MotorCommand>(
        "dome/motor/cmd",
        [](const chopper::messages::MotorCommand& cmd, void* c) {
            auto* x = static_cast<Ctx*>(c);
            (*x->count)++;
            *x->speed = cmd.value;
        },
        &ctx);

    auto input = connectedControllerInput();
    input.has_intents = true;
    input.intent_dome_rotate_right = true;
    mock_esp_timer_set(1'020'000);
    dome_pub->publish(input);
    node->process(1'020'000);

    float dome_max_speed = 0.5f;
    chopper::core::ParameterServer::getInstance().get("dome.max_speed", dome_max_speed);
    ASSERT(motor_count > 0);
    ASSERT(last_speed < 0.0f);
    ASSERT(std::fabs(last_speed) < dome_max_speed);
    ASSERT(std::fabs(last_speed) < 0.1f);
    mock_esp_timer_reset();
    PASS();
}

void test_dome_disconnect_zero_bypasses_spin_slew() {
    TEST(dome_disconnect_zero_bypasses_spin_slew);
    resetFramework();
    mock_esp_timer_set(1'000'000);

    auto node = std::make_shared<chopper::nodes::DomeNode>(nullptr, 0.5f, 1.0f, 2, false, 320);
    ASSERT(node->initialize());

    auto& broker = chopper::core::MessageBroker::getInstance();
    auto dome_pub = broker.createPublisher<chopper::messages::ControllerInput>("controller/dome");

    float last_speed = 0.0f;
    int motor_count = 0;
    struct Ctx {
        float* speed;
        int* count;
    };
    Ctx ctx{&last_speed, &motor_count};
    auto motor_sub = broker.createSubscription<chopper::messages::MotorCommand>(
        "dome/motor/cmd",
        [](const chopper::messages::MotorCommand& cmd, void* c) {
            auto* x = static_cast<Ctx*>(c);
            (*x->count)++;
            *x->speed = cmd.value;
        },
        &ctx);

    auto input = connectedControllerInput();
    input.has_intents = true;
    input.axis_rx_normalized = 0.5f;
    mock_esp_timer_set(1'500'000);
    dome_pub->publish(input);
    node->process(1'500'000);
    ASSERT(motor_count > 0);
    ASSERT(std::fabs(last_speed) > 0.01f);

    mock_esp_timer_set(1'520'000);
    chopper::messages::ControllerInput zero;
    zero.has_intents = true;
    zero.is_connected = false;
    zero.has_data = false;
    dome_pub->publish(zero);
    node->process(1'520'000);

    ASSERT_NEAR(last_speed, 0.0f, 0.001f);
    mock_esp_timer_reset();
    PASS();
}

void test_dome_connected_neutral_zero_bypasses_spin_slew() {
    TEST(dome_connected_neutral_zero_bypasses_spin_slew);
    resetFramework();
    mock_esp_timer_set(1'000'000);

    auto node = std::make_shared<chopper::nodes::DomeNode>(nullptr, 0.5f, 1.0f, 2, false, 320);
    ASSERT(node->initialize());

    auto& broker = chopper::core::MessageBroker::getInstance();
    auto dome_pub = broker.createPublisher<chopper::messages::ControllerInput>("controller/dome");

    float last_speed = 0.0f;
    int motor_count = 0;
    struct Ctx {
        float* speed;
        int* count;
    };
    Ctx ctx{&last_speed, &motor_count};
    auto motor_sub = broker.createSubscription<chopper::messages::MotorCommand>(
        "dome/motor/cmd",
        [](const chopper::messages::MotorCommand& cmd, void* c) {
            auto* x = static_cast<Ctx*>(c);
            (*x->count)++;
            *x->speed = cmd.value;
        },
        &ctx);

    auto input = connectedControllerInput();
    input.has_intents = true;
    input.axis_rx_normalized = 0.5f;
    mock_esp_timer_set(1'500'000);
    dome_pub->publish(input);
    node->process(1'500'000);
    ASSERT(motor_count > 0);
    ASSERT(std::fabs(last_speed) > 0.01f);

    input.axis_rx_normalized = 0.0f;
    mock_esp_timer_set(1'520'000);
    dome_pub->publish(input);
    node->process(1'520'000);

    ASSERT_NEAR(last_speed, 0.0f, 0.001f);
    mock_esp_timer_reset();
    PASS();
}

void test_dome_connected_neutral_preserves_tracking() {
    TEST(dome_connected_neutral_preserves_tracking);
    resetFramework();
    mock_esp_timer_set(1'000'000);

    auto node = std::make_shared<chopper::nodes::DomeNode>(nullptr, 0.5f, 1.0f, 2, false, 320);
    ASSERT(node->initialize());

    auto& broker = chopper::core::MessageBroker::getInstance();
    auto ctrl_pub = broker.createPublisher<chopper::messages::ControllerInput>("controller/dome");
    auto vision_pub = broker.createPublisher<chopper::messages::VisionResult>("vision/result");

    float last_speed = 0.0f;
    auto motor_sub = broker.createSubscription<chopper::messages::MotorCommand>(
        "dome/motor/cmd",
        [](const chopper::messages::MotorCommand& cmd, void* c) { *static_cast<float*>(c) = cmd.value; },
        &last_speed);

    auto input = connectedControllerInput();
    input.has_intents = true;
    input.intent_face_tracking_toggle = true;
    ctrl_pub->publish(input);
    ASSERT(node->isTrackingEnabled());

    chopper::messages::VisionResult v;
    v.detected = true;
    v.center_x = 80;
    v.confidence = 200;
    vision_pub->publish(v);
    node->process(1'000'000);
    ASSERT(std::fabs(node->getTrackingSpeed()) > 0.01f);
    ASSERT(std::fabs(last_speed) > 0.01f);

    input.intent_face_tracking_toggle = false;
    input.axis_rx_normalized = 0.0f;
    mock_esp_timer_set(1'020'000);
    ctrl_pub->publish(input);
    node->process(1'020'000);

    ASSERT(node->isTrackingEnabled());
    ASSERT(std::fabs(node->getTrackingSpeed()) > 0.01f);
    ASSERT(std::fabs(last_speed) > 0.01f);
    mock_esp_timer_reset();
    PASS();
}

void test_dome_connected_neutral_preserves_random_mode() {
    TEST(dome_connected_neutral_preserves_random_mode);
    resetFramework();
    mock_esp_timer_set(1'000'000);

    chopper::dome::DomePosition dome_pos;
    dome_pos.update(180, 1'000);
    auto node = std::make_shared<chopper::nodes::DomeNode>(&dome_pos, 0.5f, 1.0f, 2, false, 320);
    ASSERT(node->initialize());

    auto& broker = chopper::core::MessageBroker::getInstance();
    auto ctrl_pub = broker.createPublisher<chopper::messages::ControllerInput>("controller/dome");

    auto input = connectedControllerInput();
    input.has_intents = true;
    input.intent_dome_random_toggle = true;
    ctrl_pub->publish(input);
    input.intent_dome_random_toggle = false;
    ctrl_pub->publish(input);

    ASSERT(node->isRandomModeEnabled());
    ASSERT(dome_pos.getDomeDefaultMode() == chopper::dome::DomePosition::kRandom);

    mock_esp_timer_set(1'020'000);
    ctrl_pub->publish(input);

    ASSERT(node->isRandomModeEnabled());
    ASSERT(dome_pos.getDomeDefaultMode() == chopper::dome::DomePosition::kRandom);
    mock_esp_timer_reset();
    PASS();
}

void test_dome_disconnect_zero_clears_tracking_speed() {
    TEST(dome_disconnect_zero_clears_tracking_speed);
    resetFramework();
    mock_esp_timer_set(1'000'000);

    auto node = std::make_shared<chopper::nodes::DomeNode>(nullptr, 0.5f, 1.0f, 2, false, 320);
    ASSERT(node->initialize());

    auto& broker = chopper::core::MessageBroker::getInstance();
    auto ctrl_pub = broker.createPublisher<chopper::messages::ControllerInput>("controller/dome");
    auto vision_pub = broker.createPublisher<chopper::messages::VisionResult>("vision/result");

    float last_speed = 0.0f;
    int motor_count = 0;
    struct Ctx {
        float* speed;
        int* count;
    };
    Ctx ctx{&last_speed, &motor_count};
    auto motor_sub = broker.createSubscription<chopper::messages::MotorCommand>(
        "dome/motor/cmd",
        [](const chopper::messages::MotorCommand& cmd, void* c) {
            auto* x = static_cast<Ctx*>(c);
            (*x->count)++;
            *x->speed = cmd.value;
        },
        &ctx);

    auto input = connectedControllerInput();
    input.has_intents = true;
    input.intent_face_tracking_toggle = true;
    ctrl_pub->publish(input);
    ASSERT(node->isTrackingEnabled());

    chopper::messages::VisionResult v;
    v.detected = true;
    v.center_x = 80;
    v.confidence = 200;
    vision_pub->publish(v);
    node->process(1'000'000);
    ASSERT(motor_count > 0);
    ASSERT(std::fabs(last_speed) > 0.01f);
    ASSERT(std::fabs(node->getTrackingSpeed()) > 0.01f);

    mock_esp_timer_set(1'020'000);
    chopper::messages::ControllerInput zero;
    zero.has_intents = true;
    zero.is_connected = false;
    zero.has_data = false;
    ctrl_pub->publish(zero);
    node->process(1'020'000);

    ASSERT(!node->isTrackingEnabled());
    ASSERT_NEAR(node->getTrackingSpeed(), 0.0f, 0.001f);
    ASSERT_NEAR(last_speed, 0.0f, 0.001f);
    mock_esp_timer_reset();
    PASS();
}

void test_dome_disconnect_zero_disables_random_mode() {
    TEST(dome_disconnect_zero_disables_random_mode);
    resetFramework();
    mock_esp_timer_set(1'000'000);

    chopper::dome::DomePosition dome_pos;
    dome_pos.update(180, 1'000);
    auto node = std::make_shared<chopper::nodes::DomeNode>(&dome_pos, 0.5f, 1.0f, 2, false, 320);
    ASSERT(node->initialize());

    auto& broker = chopper::core::MessageBroker::getInstance();
    auto ctrl_pub = broker.createPublisher<chopper::messages::ControllerInput>("controller/dome");

    auto input = connectedControllerInput();
    input.has_intents = true;
    input.intent_dome_random_toggle = true;
    ctrl_pub->publish(input);
    input.intent_dome_random_toggle = false;
    ctrl_pub->publish(input);

    ASSERT(node->isRandomModeEnabled());
    ASSERT(dome_pos.getDomeDefaultMode() == chopper::dome::DomePosition::kRandom);

    mock_esp_timer_set(1'020'000);
    chopper::messages::ControllerInput zero;
    zero.has_intents = true;
    zero.is_connected = false;
    zero.has_data = false;
    ctrl_pub->publish(zero);

    ASSERT(!node->isRandomModeEnabled());
    ASSERT(dome_pos.getDomeDefaultMode() == chopper::dome::DomePosition::kOff);
    mock_esp_timer_reset();
    PASS();
}

void test_dome_eye_toggle_publishes_eye_led_colors() {
    TEST(dome_eye_toggle_publishes_eye_led_colors);
    resetFramework();

    auto node = std::make_shared<chopper::nodes::DomeNode>(nullptr, 0.5f, 1.0f, 2, false, 320);
    ASSERT(node->initialize());

    struct LedCapture {
        int count;
        uint8_t ids[4];
        chopper::messages::LEDCommand::Color colors[4];
    };
    LedCapture capture{};

    auto& broker = chopper::core::MessageBroker::getInstance();
    auto led_sub = broker.createSubscription<chopper::messages::LEDCommand>(
        "led/dome_eye/cmd",
        [](const chopper::messages::LEDCommand& cmd, void* ctx) {
            auto* cap = static_cast<LedCapture*>(ctx);
            if (cap->count < 4) {
                cap->ids[cap->count] = cmd.led_id;
                cap->colors[cap->count] = cmd.color;
            }
            cap->count++;
        },
        &capture);
    ASSERT(led_sub != nullptr);

    auto ctrl_pub = broker.createPublisher<chopper::messages::ControllerInput>("controller/dome");

    auto input = connectedControllerInput();
    input.has_intents = true;
    input.intent_eye_color_toggle = true;
    ctrl_pub->publish(input);

    ASSERT(capture.count == 2);
    ASSERT(capture.ids[0] == 1);
    ASSERT(capture.ids[1] == 2);
    for (int i = 0; i < 2; ++i) {
        ASSERT(capture.colors[i].red == 255);
        ASSERT(capture.colors[i].green == 0);
        ASSERT(capture.colors[i].blue == 0);
        ASSERT(capture.colors[i].white == 0);
    }
    PASS();
}

// ============================================================
// DomeNode face tracking tests
// ============================================================

void test_dome_tracking_toggle_via_intent() {
    TEST(dome_tracking_toggle_via_intent);
    resetFramework();

    auto node = std::make_shared<chopper::nodes::DomeNode>(nullptr, 0.5f, 1.0f, 2, false, 320);
    ASSERT(node->initialize());
    ASSERT(!node->isTrackingEnabled());

    auto& broker = chopper::core::MessageBroker::getInstance();
    auto pub = broker.createPublisher<chopper::messages::ControllerInput>("controller/dome");

    // Send toggle intent
    auto input = connectedControllerInput();
    input.has_intents = true;
    input.intent_face_tracking_toggle = true;
    pub->publish(input);

    ASSERT(node->isTrackingEnabled());

    // Send toggle again to disable
    input.intent_face_tracking_toggle = false;
    pub->publish(input);
    // Not toggled — still enabled (need rising edge)
    ASSERT(node->isTrackingEnabled());

    // Release then press again
    input.intent_face_tracking_toggle = true;
    pub->publish(input);
    ASSERT(!node->isTrackingEnabled());
    PASS();
}

void test_dome_tracking_face_right_rotates() {
    TEST(dome_tracking_face_right_rotates);
    resetFramework();

    auto node = std::make_shared<chopper::nodes::DomeNode>(nullptr, 0.5f, 1.0f, 2, false, 320);
    ASSERT(node->initialize());

    auto& broker = chopper::core::MessageBroker::getInstance();
    auto ctrl_pub = broker.createPublisher<chopper::messages::ControllerInput>("controller/dome");
    auto vision_pub = broker.createPublisher<chopper::messages::VisionResult>("vision/result");

    float last_speed = 0.0f;
    int motor_count = 0;
    struct Ctx {
        float* speed;
        int* count;
    };
    Ctx ctx{&last_speed, &motor_count};
    auto motor_sub = broker.createSubscription<chopper::messages::MotorCommand>(
        "dome/motor/cmd",
        [](const chopper::messages::MotorCommand& cmd, void* c) {
            auto* x = static_cast<Ctx*>(c);
            (*x->count)++;
            *x->speed = cmd.value;
        },
        &ctx);

    // Enable tracking
    auto input = connectedControllerInput();
    input.has_intents = true;
    input.intent_face_tracking_toggle = true;
    ctrl_pub->publish(input);
    ASSERT(node->isTrackingEnabled());

    // Publish face detected to the right
    chopper::messages::VisionResult v;
    v.detected = true;
    v.center_x = 80;  // error = 80/160 = 0.5
    v.confidence = 200;
    vision_pub->publish(v);

    // kp=0.5, error=0.5 → speed = 0.25
    ASSERT(node->getTrackingSpeed() > 0.0f);
    ASSERT_NEAR(node->getTrackingSpeed(), 0.25f, 0.01f);
    PASS();
}

void test_dome_tracking_no_face_zero_speed() {
    TEST(dome_tracking_no_face_zero_speed);
    resetFramework();

    auto node = std::make_shared<chopper::nodes::DomeNode>(nullptr, 0.5f, 1.0f, 2, false, 320);
    ASSERT(node->initialize());

    auto& broker = chopper::core::MessageBroker::getInstance();
    auto ctrl_pub = broker.createPublisher<chopper::messages::ControllerInput>("controller/dome");
    auto vision_pub = broker.createPublisher<chopper::messages::VisionResult>("vision/result");

    // Enable tracking
    auto input = connectedControllerInput();
    input.has_intents = true;
    input.intent_face_tracking_toggle = true;
    ctrl_pub->publish(input);

    // No face detected
    chopper::messages::VisionResult v;
    v.detected = false;
    v.center_x = 100;
    v.confidence = 200;
    vision_pub->publish(v);

    ASSERT_NEAR(node->getTrackingSpeed(), 0.0f, 0.001f);
    PASS();
}

void test_dome_tracking_stale_vision_times_out_to_zero() {
    TEST(dome_tracking_stale_vision_times_out_to_zero);
    resetFramework();
    mock_esp_timer_set(1'000'000);

    auto node = std::make_shared<chopper::nodes::DomeNode>(nullptr, 0.5f, 1.0f, 2, false, 320);
    ASSERT(node->initialize());

    auto& broker = chopper::core::MessageBroker::getInstance();
    auto ctrl_pub = broker.createPublisher<chopper::messages::ControllerInput>("controller/dome");
    auto vision_pub = broker.createPublisher<chopper::messages::VisionResult>("vision/result");

    float last_speed = 0.0f;
    auto motor_sub = broker.createSubscription<chopper::messages::MotorCommand>(
        "dome/motor/cmd",
        [](const chopper::messages::MotorCommand& cmd, void* c) { *static_cast<float*>(c) = cmd.value; },
        &last_speed);

    auto input = connectedControllerInput();
    input.has_intents = true;
    input.intent_face_tracking_toggle = true;
    ctrl_pub->publish(input);
    ASSERT(node->isTrackingEnabled());

    chopper::messages::VisionResult v;
    v.detected = true;
    v.center_x = 80;
    v.confidence = 200;
    vision_pub->publish(v);
    node->process(1'000'000);
    ASSERT(std::fabs(node->getTrackingSpeed()) > 0.01f);
    ASSERT(std::fabs(last_speed) > 0.01f);

    mock_esp_timer_set(1'600'000);
    node->process(1'600'000);

    ASSERT(node->isTrackingEnabled());
    ASSERT_NEAR(node->getTrackingSpeed(), 0.0f, 0.001f);
    ASSERT_NEAR(last_speed, 0.0f, 0.001f);
    mock_esp_timer_reset();
    PASS();
}

void test_dome_tracking_enabled_suppresses_auto_motion() {
    TEST(dome_tracking_enabled_suppresses_auto_motion);
    resetFramework();

    chopper::dome::DomePosition dome_pos;
    dome_pos.update(180, 0);
    dome_pos.setDomeHomePosition(0);

    auto node = std::make_shared<chopper::nodes::DomeNode>(&dome_pos, 0.5f, 1.0f, 2, false, 320);
    ASSERT(node->initialize());

    auto& broker = chopper::core::MessageBroker::getInstance();
    auto ctrl_pub = broker.createPublisher<chopper::messages::ControllerInput>("controller/dome");

    float last_speed = 0.0f;
    auto motor_sub = broker.createSubscription<chopper::messages::MotorCommand>(
        "dome/motor/cmd",
        [](const chopper::messages::MotorCommand& cmd, void* c) { *static_cast<float*>(c) = cmd.value; },
        &last_speed);

    auto input = connectedControllerInput();
    input.has_intents = true;
    input.intent_dome_random_toggle = true;
    ctrl_pub->publish(input);
    input.intent_dome_random_toggle = false;
    ctrl_pub->publish(input);
    ASSERT(node->isRandomModeEnabled());

    input.intent_face_tracking_toggle = true;
    ctrl_pub->publish(input);
    ASSERT(node->isTrackingEnabled());

    node->setDomeMovedManually(true);
    dome_pos.setDomeMode(chopper::dome::DomePosition::kHome, 1'000);
    node->process(10'000'000);

    ASSERT_NEAR(last_speed, 0.0f, 0.001f);
    PASS();
}

void test_dome_tracking_disabled_ignores_vision() {
    TEST(dome_tracking_disabled_ignores_vision);
    resetFramework();

    auto node = std::make_shared<chopper::nodes::DomeNode>(nullptr, 0.5f, 1.0f, 2, false, 320);
    ASSERT(node->initialize());
    ASSERT(!node->isTrackingEnabled());

    auto& broker = chopper::core::MessageBroker::getInstance();
    auto vision_pub = broker.createPublisher<chopper::messages::VisionResult>("vision/result");

    int motor_count = 0;
    auto motor_sub = broker.createSubscription<chopper::messages::MotorCommand>(
        "dome/motor/cmd",
        [](const chopper::messages::MotorCommand&, void* c) { (*static_cast<int*>(c))++; },
        &motor_count);

    chopper::messages::VisionResult v;
    v.detected = true;
    v.center_x = 80;
    v.confidence = 200;
    vision_pub->publish(v);

    // Tracking disabled — no motor command from vision
    ASSERT(motor_count == 0);
    PASS();
}

void test_dome_tracking_toggle_publishes_tracking_cmd() {
    TEST(dome_tracking_toggle_publishes_tracking_cmd);
    resetFramework();

    auto node = std::make_shared<chopper::nodes::DomeNode>(nullptr, 0.5f, 1.0f, 2, false, 320);
    ASSERT(node->initialize());

    auto& broker = chopper::core::MessageBroker::getInstance();
    auto ctrl_pub = broker.createPublisher<chopper::messages::ControllerInput>("controller/dome");

    bool last_enabled = false;
    int tracking_count = 0;
    struct Ctx {
        bool* enabled;
        int* count;
    };
    Ctx ctx{&last_enabled, &tracking_count};
    auto tracking_sub = broker.createSubscription<chopper::messages::TrackingCommand>(
        "openmv/tracking/cmd",
        [](const chopper::messages::TrackingCommand& cmd, void* c) {
            auto* x = static_cast<Ctx*>(c);
            (*x->count)++;
            *x->enabled = cmd.enabled;
        },
        &ctx);

    // Enable
    auto input = connectedControllerInput();
    input.has_intents = true;
    input.intent_face_tracking_toggle = true;
    ctrl_pub->publish(input);

    ASSERT(tracking_count == 1);
    ASSERT(last_enabled == true);

    // Disable
    input.intent_face_tracking_toggle = false;
    ctrl_pub->publish(input);
    input.intent_face_tracking_toggle = true;
    ctrl_pub->publish(input);

    ASSERT(tracking_count == 2);
    ASSERT(last_enabled == false);
    PASS();
}

// ============================================================
// BodyLedNode tests
// ============================================================

static uint8_t g_led_brightness = 0;
static int g_led_write_count = 0;

static void testLedWrite(uint8_t brightness, void* /*ctx*/) {
    g_led_brightness = brightness;
    g_led_write_count++;
}

void test_body_led_starts_at_zero() {
    TEST(body_led_starts_at_zero);
    resetFramework();
    g_led_brightness = 99;
    g_led_write_count = 0;

    chopper::nodes::BodyLedNode node(&testLedWrite, nullptr, 1'000'000);
    ASSERT(node.initialize());
    node.activate();

    // First call at time 0 → brightness should be 0
    node.process(1'000);
    ASSERT(g_led_write_count == 1);
    ASSERT(g_led_brightness == 0);
    PASS();
}

void test_body_led_peaks_at_half_period() {
    TEST(body_led_peaks_at_half_period);
    resetFramework();
    g_led_brightness = 0;

    constexpr uint64_t period = 1'000'000;
    chopper::nodes::BodyLedNode node(&testLedWrite, nullptr, period);
    ASSERT(node.initialize());
    node.activate();

    // Seed start time
    node.process(0);

    // At half period → brightness should be at or near 255
    node.process(period / 2);
    ASSERT(g_led_brightness >= 250);
    PASS();
}

void test_body_led_returns_to_zero_at_full_period() {
    TEST(body_led_returns_to_zero_at_full_period);
    resetFramework();
    g_led_brightness = 99;

    constexpr uint64_t period = 1'000'000;
    chopper::nodes::BodyLedNode node(&testLedWrite, nullptr, period);
    ASSERT(node.initialize());
    node.activate();

    // Seed start time
    node.process(0);

    // At full period → brightness wraps back to 0
    node.process(period);
    ASSERT(g_led_brightness <= 5);
    PASS();
}

void test_body_led_emergency_stop_turns_off() {
    TEST(body_led_emergency_stop_turns_off);
    resetFramework();
    g_led_brightness = 99;

    chopper::nodes::BodyLedNode node(&testLedWrite);
    ASSERT(node.initialize());
    node.activate();

    // Advance to mid-fade so LED is on
    node.process(0);
    node.process(250'000);
    ASSERT(g_led_brightness > 0);

    // Emergency stop should turn off
    node.emergencyStop();
    ASSERT(g_led_brightness == 0);
    PASS();
}

void test_body_led_publishes_led_command() {
    TEST(body_led_publishes_led_command);
    resetFramework();

    chopper::nodes::BodyLedNode node(&testLedWrite);
    ASSERT(node.initialize());
    node.activate();

    // Subscribe to the LED topic
    uint8_t received_brightness = 0;
    uint8_t received_led_id = 255;
    bool received = false;
    auto sub = chopper::core::MessageBroker::getInstance().createSubscription<chopper::messages::LEDCommand>(
        "led/front/cmd",
        [](const chopper::messages::LEDCommand& cmd, void* ctx) {
            auto* flag = static_cast<bool*>(ctx);
            // Store via globals since lambda captures aren't available with fn ptr
            *flag = true;
        },
        &received);

    // Process at mid-fade
    node.process(0);
    node.process(255'000);

    ASSERT(received);
    PASS();
}

void test_body_led_null_callback_fails_init() {
    TEST(body_led_null_callback_fails_init);
    resetFramework();

    chopper::nodes::BodyLedNode node(nullptr);
    ASSERT(!node.initialize());
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
    test_drive_node_disconnect_zero_bypasses_slew();
    test_drive_node_connected_neutral_zero_bypasses_slew();
    test_drive_node_carpet_mode_toggle();
    test_drive_node_transient_carpet_mode_active_intent();

    // PeriscopeNode
    test_periscope_intent_toggles_lift();
    test_periscope_led_colors_only_when_lifted();
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
    test_body_utility_intent_toggles_open_closed();
    test_body_doors_intents_toggle_individual_doors();

    // SoundNode
    test_sound_a_plays_correct_track();
    test_sound_b_plays_correct_track();
    test_sound_misc_start_plays_track();
    test_sound_drive_sl_sr_changes_volume();

    // Intent-path tests
    test_dome_arms_intent_toggles_doors();
    test_body_utility_intent_extends();
    test_drive_node_carpet_mode_via_intent();
    test_sound_a_via_intent();
    test_sound_volume_via_drive_intent();

    // DomeNode button rotation
    test_dome_drive_l2_publishes_positive_speed();
    test_dome_dome_l2_publishes_negative_speed();
    test_dome_no_button_publishes_zero();
    test_dome_analog_rx_publishes_proportional_speed();
    test_dome_first_manual_command_slews_from_zero();
    test_dome_disconnect_zero_bypasses_spin_slew();
    test_dome_connected_neutral_zero_bypasses_spin_slew();
    test_dome_connected_neutral_preserves_tracking();
    test_dome_connected_neutral_preserves_random_mode();
    test_dome_disconnect_zero_clears_tracking_speed();
    test_dome_disconnect_zero_disables_random_mode();
    test_dome_eye_toggle_publishes_eye_led_colors();

    // DomeNode face tracking
    test_dome_tracking_toggle_via_intent();
    test_dome_tracking_face_right_rotates();
    test_dome_tracking_no_face_zero_speed();
    test_dome_tracking_stale_vision_times_out_to_zero();
    test_dome_tracking_enabled_suppresses_auto_motion();
    test_dome_tracking_disabled_ignores_vision();
    test_dome_tracking_toggle_publishes_tracking_cmd();

    // BodyLedNode
    test_body_led_starts_at_zero();
    test_body_led_peaks_at_half_period();
    test_body_led_returns_to_zero_at_full_period();
    test_body_led_emergency_stop_turns_off();
    test_body_led_publishes_led_command();
    test_body_led_null_callback_fails_init();

    printf("\n=== Results: %d/%d passed ===\n", pass_count, test_count);
    return (pass_count == test_count) ? 0 : 1;
}
