// Host-side integration test for the complete Chopper framework.
// Verifies end-to-end message flow from pub/sub through bridge nodes
// to mock hardware drivers, and tests safety system integration.
//
// Compile:
//   g++ -std=c++20 -I test/mocks -I main/include \
//       test/test_integration.cpp \
//       main/chopper/core/Node.cpp \
//       main/chopper/core/Publisher.cpp \
//       main/chopper/core/Subscription.cpp \
//       main/chopper/core/MessageBroker.cpp \
//       main/chopper/core/PublishingNode.cpp \
//       main/chopper/core/Message.cpp \
//       main/chopper/core/TimerManager.cpp \
//       main/chopper/core/ParameterServer.cpp \
//       main/chopper/safety/DegradationManager.cpp \
//       main/chopper/safety/EmergencyStopChain.cpp \
//       main/chopper/safety/SafetyManager.cpp \
//       main/chopper/hal/DriverManager.cpp \
//       main/chopper/Application.cpp \
//       -o test/test_integration -pthread

#include <cstdio>
#include <cassert>
#include <cstring>
#include <memory>

// Core framework
#include "chopper/chopper_limits.h"
#include "chopper/core/Message.h"
#include "chopper/core/Node.h"
#include "chopper/core/Publisher.h"
#include "chopper/core/Subscription.h"
#include "chopper/core/MessageBroker.h"
#include "chopper/core/PublishingNode.h"
#include "chopper/core/TimerManager.h"
#include "chopper/messages/CommonMessages.h"

// HAL
#include "chopper/hal/IDriver.h"
#include "chopper/hal/IMotorDriver.h"
#include "chopper/hal/IServoController.h"
#include "chopper/hal/IAudioDriver.h"
#include "chopper/hal/MockMotorDriver.h"
#include "chopper/hal/MockServoDriver.h"
#include "chopper/hal/MockAudioDriver.h"

// Safety
#include "chopper/safety/SafetyManager.h"
#include "chopper/safety/DegradationManager.h"
#include "chopper/safety/EmergencyStopChain.h"

// Bluetooth
#include "chopper/bluetooth/ControllerManager.h"

// Bridge nodes
#include "chopper/nodes/MotorBridgeNode.h"
#include "chopper/nodes/ServoBridgeNode.h"
#include "chopper/nodes/AudioBridgeNode.h"
#include "chopper/nodes/SafetyNode.h"

// Application
#include "chopper/Application.h"

// ---- Test helpers ----

static int test_count = 0;
static int pass_count = 0;

#define TEST(name) \
    do { test_count++; printf("TEST: %s ... ", #name); } while(0)
#define PASS() \
    do { pass_count++; printf("PASS\n"); } while(0)
#define ASSERT(cond) \
    do { if (!(cond)) { printf("FAIL at %s:%d: %s\n", __FILE__, __LINE__, #cond); return; } } while(0)

// ---- Test: Motor bridge node end-to-end ----

void test_motor_bridge_e2e() {
    TEST(motor_bridge_end_to_end);

    chopper::hal::MockMotorDriver leftMotor("left");
    chopper::hal::MockMotorDriver rightMotor("right");

    auto leftNode = std::make_shared<chopper::nodes::MotorBridgeNode>(
        "left_bridge", "drive/cmd", &leftMotor, 0);
    auto rightNode = std::make_shared<chopper::nodes::MotorBridgeNode>(
        "right_bridge", "drive/cmd", &rightMotor, 1);

    // Create a publisher for motor commands
    auto& broker = chopper::core::MessageBroker::getInstance();
    auto pub = broker.createPublisher<chopper::messages::MotorCommand>("drive/cmd");

    // Initialize bridge nodes (registers subscriptions)
    ASSERT(leftNode->initialize());
    ASSERT(rightNode->initialize());

    // Publish a command for motor 0 (left)
    chopper::messages::MotorCommand cmd;
    cmd.motor_id = 0;
    cmd.command_type = chopper::messages::MotorCommand::CommandType::SET_SPEED;
    cmd.value = 0.75f;
    pub->publish(cmd);

    // Left motor should have received the command
    ASSERT(leftMotor.get() == 0.75f);
    // Right motor should NOT have received it (wrong motor_id)
    ASSERT(rightMotor.get() == 0.0f);

    // Publish a command for motor 1 (right)
    cmd.motor_id = 1;
    cmd.value = -0.5f;
    pub->publish(cmd);

    ASSERT(leftMotor.get() == 0.75f);  // unchanged
    ASSERT(rightMotor.get() == -0.5f);

    // Test STOP command
    cmd.motor_id = 0;
    cmd.command_type = chopper::messages::MotorCommand::CommandType::STOP;
    pub->publish(cmd);

    ASSERT(leftMotor.get() == 0.0f);

    PASS();
}

// ---- Test: Servo bridge node end-to-end ----

void test_servo_bridge_e2e() {
    TEST(servo_bridge_end_to_end);

    chopper::hal::MockServoDriver servoCtrl("body_servos", 12);

    auto servoNode = std::make_shared<chopper::nodes::ServoBridgeNode>(
        "servo_bridge", "servo/cmd", &servoCtrl);

    auto& broker = chopper::core::MessageBroker::getInstance();
    auto pub = broker.createPublisher<chopper::messages::ServoCommand>("servo/cmd");

    ASSERT(servoNode->initialize());

    // Enable a channel
    chopper::messages::ServoCommand enableCmd;
    enableCmd.servo_id = 3;
    enableCmd.command_type = chopper::messages::ServoCommand::CommandType::ENABLE;
    pub->publish(enableCmd);

    ASSERT(servoCtrl.isEnabled(3));

    // Set position (value is raw pulse-width microseconds)
    chopper::messages::ServoCommand posCmd;
    posCmd.servo_id = 3;
    posCmd.command_type = chopper::messages::ServoCommand::CommandType::SET_POSITION;
    posCmd.value = 1500.0f;
    pub->publish(posCmd);

    ASSERT(servoCtrl.getPosition(3) == 1500);

    // Set position at min
    posCmd.value = 500.0f;
    pub->publish(posCmd);
    ASSERT(servoCtrl.getPosition(3) == 500);

    // Set position at max
    posCmd.value = 2500.0f;
    pub->publish(posCmd);
    ASSERT(servoCtrl.getPosition(3) == 2500);

    // Disable channel
    chopper::messages::ServoCommand disableCmd;
    disableCmd.servo_id = 3;
    disableCmd.command_type = chopper::messages::ServoCommand::CommandType::DISABLE;
    pub->publish(disableCmd);

    ASSERT(!servoCtrl.isEnabled(3));

    PASS();
}

// ---- Test: Audio bridge node end-to-end ----

void test_audio_bridge_e2e() {
    TEST(audio_bridge_end_to_end);

    chopper::hal::MockAudioDriver audioDriver("mp3");

    auto audioNode = std::make_shared<chopper::nodes::AudioBridgeNode>(
        "audio_bridge", "audio/cmd", &audioDriver);

    auto& broker = chopper::core::MessageBroker::getInstance();
    auto pub = broker.createPublisher<chopper::messages::AudioCommand>("audio/cmd");

    ASSERT(audioNode->initialize());

    // Set volume
    chopper::messages::AudioCommand volCmd;
    volCmd.command_type = chopper::messages::AudioCommand::CommandType::SET_VOLUME;
    volCmd.volume = 200;
    pub->publish(volCmd);

    ASSERT(audioDriver.getVolume() == 200);

    // Play track
    chopper::messages::AudioCommand playCmd;
    playCmd.command_type = chopper::messages::AudioCommand::CommandType::PLAY_TRACK;
    playCmd.track_id = 5;
    pub->publish(playCmd);

    ASSERT(audioDriver.isPlaying());
    ASSERT(audioDriver.getLastTrack() == 5);
    ASSERT(audioDriver.getTriggerCount() == 1);

    // Stop
    chopper::messages::AudioCommand stopCmd;
    stopCmd.command_type = chopper::messages::AudioCommand::CommandType::STOP;
    pub->publish(stopCmd);

    ASSERT(!audioDriver.isPlaying());

    PASS();
}

// ---- Test: Safety node publishes status on mode change ----

void test_safety_node_status() {
    TEST(safety_node_publishes_status);

    chopper::safety::SafetyManager safetyMgr;

    auto safetyNode = std::make_shared<chopper::nodes::SafetyNode>(&safetyMgr);

    // Subscribe to system status
    auto& broker = chopper::core::MessageBroker::getInstance();
    chopper::messages::SystemStatus lastStatus;
    int statusCount = 0;

    auto sub = broker.createSubscription<chopper::messages::SystemStatus>(
        "system/status",
        [](const chopper::messages::SystemStatus& msg, void* ctx) {
            auto* count = static_cast<int*>(ctx);
            (*count)++;
        },
        &statusCount);

    ASSERT(safetyNode->initialize());
    safetyNode->activate();

    // Process with initial SAFE_STOP mode — no status change, no publish
    safetyNode->process(1000);
    ASSERT(statusCount == 0);

    // Force degradation mode change (SAFE_STOP → EMERGENCY_STOP)
    safetyMgr.getDegradationManager().forceMode(
        chopper::safety::DegradationMode::EMERGENCY_STOP);

    // Process again — should detect mode change and publish
    safetyNode->process(2000);
    ASSERT(statusCount == 1);

    // Process again without mode change — no new publish
    safetyNode->process(3000);
    ASSERT(statusCount == 1);

    PASS();
}

// ---- Test: Emergency stop propagates through bridge nodes ----

void test_emergency_stop_propagation() {
    TEST(emergency_stop_propagates_to_drivers);

    chopper::hal::MockMotorDriver motor("test_motor");
    chopper::hal::MockServoDriver servo("test_servo", 6);
    chopper::hal::MockAudioDriver audio("test_audio");

    auto motorNode = std::make_shared<chopper::nodes::MotorBridgeNode>(
        "motor_estop", "estop/motor", &motor, 0);
    auto servoNode = std::make_shared<chopper::nodes::ServoBridgeNode>(
        "servo_estop", "estop/servo", &servo);
    auto audioNode = std::make_shared<chopper::nodes::AudioBridgeNode>(
        "audio_estop", "estop/audio", &audio);

    ASSERT(motorNode->initialize());
    ASSERT(servoNode->initialize());
    ASSERT(audioNode->initialize());

    // Set some state first
    motor.init();
    motor.set(0.8f);
    servo.init();
    servo.enable(0);
    servo.setPosition(0, 1500);
    audio.init();
    audio.trigger(1);

    ASSERT(motor.get() == 0.8f);
    ASSERT(audio.isPlaying());

    // Call emergencyStop on each node
    motorNode->emergencyStop();
    servoNode->emergencyStop();
    audioNode->emergencyStop();

    // Motor should be stopped
    ASSERT(motor.get() == 0.0f);
    // All servo channels should be disabled
    ASSERT(!servo.isEnabled(0));
    // Audio should be stopped
    ASSERT(!audio.isPlaying());

    PASS();
}

// ---- Test: Executor e-stop → SafetyManager integration ----

void test_executor_safety_wiring() {
    TEST(executor_estop_wires_to_safety_manager);

    chopper::safety::SafetyManager safetyMgr;
    chopper::core::Executor executor;

    // Wire e-stop callback
    executor.setEmergencyStopCallback(
        &chopper::safety::SafetyManager::onExecutorEStop, &safetyMgr);

    ASSERT(!safetyMgr.isEmergencyStopped());

    // Trigger e-stop through executor
    executor.emergencyStop("test reason");

    ASSERT(safetyMgr.isEmergencyStopped());
    ASSERT(safetyMgr.getDegradationManager().getCurrentMode()
           == chopper::safety::DegradationMode::EMERGENCY_STOP);

    // Reset and verify
    ASSERT(safetyMgr.resetEmergencyStop());
    ASSERT(!safetyMgr.isEmergencyStopped());
    ASSERT(safetyMgr.getDegradationManager().getCurrentMode()
           == chopper::safety::DegradationMode::SAFE_STOP);

    PASS();
}

// ---- Test: ControllerManager with disconnect fallback ----

void test_controller_disconnect_fallback() {
    TEST(controller_disconnect_fallback_behavior);

    chopper::bluetooth::ControllerManager ctlMgr;
    chopper::bluetooth::FirstAvailablePolicy policy;
    ctlMgr.getRoleManager().setPolicy(policy.getPolicy());
    chopper::bluetooth::DefaultDisconnectHandler handler;
    ctlMgr.setDisconnectBehavior(handler.getBehavior());

    // Connect a controller
    chopper::bluetooth::MacAddress mac;
    mac.addr[0] = 0xAA; mac.addr[1] = 0xBB; mac.addr[2] = 0xCC;
    mac.addr[3] = 0xDD; mac.addr[4] = 0xEE; mac.addr[5] = 0x01;
    int8_t slot = ctlMgr.onConnect(mac, 0x01, 0, 0, 1000);
    ASSERT(slot >= 0);

    // Record some input
    chopper::messages::ControllerInput input;
    input.axis_x = 100;
    input.axis_y = 200;
    input.is_connected = true;
    ctlMgr.recordInput(slot, 1100, input);

    // Assign DRIVE role (default first-available should do this)
    // Verify the slot is active
    ASSERT(ctlMgr.getSlot(slot).isActive());

    // Disconnect
    ctlMgr.onDisconnect(slot, 1200);

    // For DRIVE role, fallback should publish one zero cycle before clearing.
    ASSERT(!ctlMgr.getSlot(slot).isEmpty());
    chopper::messages::ControllerInput fallback{};
    ASSERT(ctlMgr.getFallbackInput(slot, fallback));
    ASSERT(!fallback.is_connected);
    ASSERT(fallback.axis_x == 0);
    ASSERT(fallback.axis_y == 0);

    ctlMgr.update(1201);
    ASSERT(ctlMgr.getSlot(slot).isEmpty());

    PASS();
}

// ---- Test: Full pipeline - Controller Input → Motor Command → Mock Driver ----

void test_full_pipeline() {
    TEST(full_pipeline_input_to_driver);

    // This simulates the complete data path:
    // ControllerInput → processing node → MotorCommand → MotorBridgeNode → MockMotorDriver

    chopper::hal::MockMotorDriver leftMotor("pipeline_left");
    chopper::hal::MockMotorDriver rightMotor("pipeline_right");

    // Create a simple processing node that converts controller input to motor commands
    class DriveLogicNode : public chopper::core::PublishingNode {
    public:
        DriveLogicNode() : PublishingNode("drive_logic") {}

        bool initialize() override {
            input_sub_ = createSubscription<chopper::messages::ControllerInput>(
                "input/drive", &DriveLogicNode::onInput, this);
            motor_pub_ = createPublisher<chopper::messages::MotorCommand>("pipeline/drive");
            return input_sub_ && motor_pub_;
        }

        void process(uint64_t) override {}
        void emergencyStop() override {}

    private:
        void onInput(const chopper::messages::ControllerInput& input) {
            // Simple tank-drive mixing:
            // left  = y + x
            // right = y - x
            float x = input.axis_x_normalized;
            float y = input.axis_y_normalized;

            chopper::messages::MotorCommand leftCmd;
            leftCmd.motor_id = 0;
            leftCmd.command_type = chopper::messages::MotorCommand::CommandType::SET_SPEED;
            leftCmd.value = y + x;
            if (leftCmd.value > 1.0f) leftCmd.value = 1.0f;
            if (leftCmd.value < -1.0f) leftCmd.value = -1.0f;
            motor_pub_->publish(leftCmd);

            chopper::messages::MotorCommand rightCmd;
            rightCmd.motor_id = 1;
            rightCmd.command_type = chopper::messages::MotorCommand::CommandType::SET_SPEED;
            rightCmd.value = y - x;
            if (rightCmd.value > 1.0f) rightCmd.value = 1.0f;
            if (rightCmd.value < -1.0f) rightCmd.value = -1.0f;
            motor_pub_->publish(rightCmd);
        }

        chopper::core::TypedSubscriptionPtr<chopper::messages::ControllerInput> input_sub_;
        chopper::core::TypedPublisherPtr<chopper::messages::MotorCommand> motor_pub_;
    };

    // Set up the pipeline
    auto driveLogic = std::make_shared<DriveLogicNode>();
    auto leftBridge = std::make_shared<chopper::nodes::MotorBridgeNode>(
        "left_pipeline", "pipeline/drive", &leftMotor, 0);
    auto rightBridge = std::make_shared<chopper::nodes::MotorBridgeNode>(
        "right_pipeline", "pipeline/drive", &rightMotor, 1);

    // Create a publisher to simulate controller input
    auto& broker = chopper::core::MessageBroker::getInstance();
    auto inputPub = broker.createPublisher<chopper::messages::ControllerInput>("input/drive");

    ASSERT(driveLogic->initialize());
    ASSERT(leftBridge->initialize());
    ASSERT(rightBridge->initialize());

    // Simulate joystick: forward (y=0.5, x=0.0) → both motors at 0.5
    chopper::messages::ControllerInput input;
    input.axis_y_normalized = 0.5f;
    input.axis_x_normalized = 0.0f;
    inputPub->publish(input);

    ASSERT(leftMotor.get() == 0.5f);
    ASSERT(rightMotor.get() == 0.5f);

    // Turn right (y=0.5, x=0.3) → left=0.8, right=0.2
    input.axis_y_normalized = 0.5f;
    input.axis_x_normalized = 0.3f;
    inputPub->publish(input);

    // Allow floating point comparison with small epsilon
    ASSERT(leftMotor.get() > 0.79f && leftMotor.get() < 0.81f);
    ASSERT(rightMotor.get() > 0.19f && rightMotor.get() < 0.21f);

    // Full reverse (y=-1.0, x=0.0)
    input.axis_y_normalized = -1.0f;
    input.axis_x_normalized = 0.0f;
    inputPub->publish(input);

    ASSERT(leftMotor.get() == -1.0f);
    ASSERT(rightMotor.get() == -1.0f);

    PASS();
}

// ---- Test: Timer integration with bridge nodes ----

void test_timer_integration() {
    TEST(timer_fires_and_publishes);

    auto& timerMgr = chopper::core::TimerManager::getInstance();

    // Create a timer that publishes a motor command
    struct TimerContext {
        chopper::core::TypedPublisherPtr<chopper::messages::MotorCommand> pub;
        int fire_count;
    };

    auto& broker = chopper::core::MessageBroker::getInstance();
    auto pub = broker.createPublisher<chopper::messages::MotorCommand>("timer/motor");

    TimerContext ctx{pub, 0};

    uint32_t timerId = timerMgr.createTimer(
        10000, // 10ms period
        [](void* c) {
            auto* tc = static_cast<TimerContext*>(c);
            tc->fire_count++;
            chopper::messages::MotorCommand cmd;
            cmd.motor_id = 0;
            cmd.value = 0.1f * tc->fire_count;
            cmd.command_type = chopper::messages::MotorCommand::CommandType::SET_SPEED;
            tc->pub->publish(cmd);
        },
        &ctx,
        false);  // repeating

    ASSERT(timerId > 0);

    // Set up a mock motor to receive the commands
    chopper::hal::MockMotorDriver motor("timer_motor");
    auto bridge = std::make_shared<chopper::nodes::MotorBridgeNode>(
        "timer_bridge", "timer/motor", &motor, 0);
    ASSERT(bridge->initialize());

    // First tick initializes the deadline
    timerMgr.tick(100000);
    ASSERT(ctx.fire_count == 0);

    // Tick past the deadline
    timerMgr.tick(110001);
    ASSERT(ctx.fire_count == 1);
    ASSERT(motor.get() > 0.09f && motor.get() < 0.11f);

    // Tick again
    timerMgr.tick(120002);
    ASSERT(ctx.fire_count == 2);
    ASSERT(motor.get() > 0.19f && motor.get() < 0.21f);

    timerMgr.cancelTimer(timerId);

    PASS();
}

// ---- Test: Application init with mock hardware ----

void test_application_init() {
    TEST(application_initializes_with_mock_hardware);

    chopper::hal::MockMotorDriver leftMotor("app_left");
    chopper::hal::MockMotorDriver rightMotor("app_right");
    chopper::hal::MockMotorDriver domeMotor("app_dome");
    chopper::hal::MockServoDriver bodyServos("app_body", 12);
    chopper::hal::MockAudioDriver audio("app_audio");

    chopper::Application app;

    ASSERT(app.addMotor(0, &leftMotor, "drive/cmd"));
    ASSERT(app.addMotor(1, &rightMotor, "drive/cmd"));
    ASSERT(app.addMotor(2, &domeMotor, "dome/cmd"));
    ASSERT(app.addServoController(&bodyServos, "servo/body"));
    ASSERT(app.addAudio(&audio, "audio/cmd"));

    // Preload non-idle actuator state to verify init drives a safe startup state.
    leftMotor.set(0.8f);
    rightMotor.set(-0.6f);
    domeMotor.set(0.4f);
    bodyServos.enable(0);
    bodyServos.enable(1);
    bodyServos.setPosition(0, 1500);
    bodyServos.setPosition(1, 1700);

    ASSERT(app.init());
    ASSERT(app.isInitialized());
    ASSERT(leftMotor.get() == 0.0f);
    ASSERT(rightMotor.get() == 0.0f);
    ASSERT(domeMotor.get() == 0.0f);
    ASSERT(!bodyServos.isEnabled(0));
    ASSERT(!bodyServos.isEnabled(1));

    // Verify e-stop wiring
    ASSERT(!app.getSafetyManager().isEmergencyStopped());
    app.emergencyStop("test");
    ASSERT(app.getSafetyManager().isEmergencyStopped());

    PASS();
}

// ---- Test: Application rejects double init ----

void test_application_double_init() {
    TEST(application_rejects_double_init);

    chopper::Application app;
    ASSERT(app.init());
    ASSERT(!app.init());  // second init should fail

    PASS();
}

// ---- Test: Application rejects too many motors ----

void test_application_motor_limits() {
    TEST(application_rejects_excess_motors);

    chopper::hal::MockMotorDriver motors[5] = {
        chopper::hal::MockMotorDriver("m0"),
        chopper::hal::MockMotorDriver("m1"),
        chopper::hal::MockMotorDriver("m2"),
        chopper::hal::MockMotorDriver("m3"),
        chopper::hal::MockMotorDriver("m4"),
    };

    chopper::Application app;
    ASSERT(app.addMotor(0, &motors[0]));
    ASSERT(app.addMotor(1, &motors[1]));
    ASSERT(app.addMotor(2, &motors[2]));
    ASSERT(app.addMotor(3, &motors[3]));
    ASSERT(!app.addMotor(4, &motors[4]));  // exceeds kMaxMotors=4

    PASS();
}

// ---- Test: Cross-subsystem - safety monitors motor bridges ----

void test_cross_subsystem_safety() {
    TEST(safety_manager_monitors_motor_activity);

    chopper::safety::SafetyManager safetyMgr;

    // DegradationManager starts at SAFE_STOP; upgrade to FULL_OPERATION
    // (upgrade one step at a time: SAFE_STOP → ESSENTIAL → REDUCED → FULL)
    safetyMgr.getDegradationManager().forceMode(
        chopper::safety::DegradationMode::FULL_OPERATION);
    ASSERT(safetyMgr.getDegradationManager().getCurrentMode()
           == chopper::safety::DegradationMode::FULL_OPERATION);

    // Register a motor with safety monitor (name, timeout_ms, stop_cb, ctx)
    int stop_called = 0;
    uint8_t motor_id = safetyMgr.getMotorSafetyMonitor().registerMotor(
        "test_motor", 100,  // 100ms timeout
        [](uint8_t, void* ctx) { (*static_cast<int*>(ctx))++; },
        &stop_called);
    ASSERT(motor_id != 0xFF);

    // Feed the motor (uses esp_timer_get_time() internally)
    safetyMgr.getMotorSafetyMonitor().feed(motor_id);

    // Check with a timestamp just after feed — should be fine
    uint64_t feed_time = static_cast<uint64_t>(esp_timer_get_time());
    safetyMgr.update(feed_time + 50000);  // 50ms later
    ASSERT(!safetyMgr.isEmergencyStopped());
    ASSERT(safetyMgr.getDegradationManager().getCurrentMode()
           == chopper::safety::DegradationMode::FULL_OPERATION);

    // Let it time out (don't feed, advance past timeout)
    // The motor was last fed at feed_time; timeout is 100ms = 100000us
    safetyMgr.update(feed_time + 200000);  // 200ms later

    // Should have degraded (motor timeout → ESSENTIAL_ONLY)
    ASSERT(safetyMgr.getDegradationManager().getCurrentMode()
           == chopper::safety::DegradationMode::ESSENTIAL_ONLY);

    PASS();
}

// ---- Test: Bluetooth controller manager with role assignment ----

void test_bluetooth_role_assignment() {
    TEST(bluetooth_controller_role_assignment);

    chopper::bluetooth::ControllerManager ctlMgr;

    // Connect first controller — should get DRIVE role
    chopper::bluetooth::MacAddress mac1;
    mac1.addr[0] = 0x01; mac1.addr[1] = 0x02; mac1.addr[2] = 0x03;
    mac1.addr[3] = 0x04; mac1.addr[4] = 0x05; mac1.addr[5] = 0x06;
    int8_t slot1 = ctlMgr.onConnect(mac1, 0x01, 0, 0, 1000);
    ASSERT(slot1 >= 0);
    ASSERT(ctlMgr.getSlot(slot1).isActive());

    // Connect second controller — should get DOME role
    chopper::bluetooth::MacAddress mac2;
    mac2.addr[0] = 0xAA; mac2.addr[1] = 0xBB; mac2.addr[2] = 0xCC;
    mac2.addr[3] = 0xDD; mac2.addr[4] = 0xEE; mac2.addr[5] = 0xFF;
    int8_t slot2 = ctlMgr.onConnect(mac2, 0x01, 0, 0, 2000);
    ASSERT(slot2 >= 0);
    ASSERT(slot1 != slot2);

    // Both should be active
    ASSERT(ctlMgr.getActiveCount() == 2);

    // Disconnect first
    ctlMgr.onDisconnect(slot1, 3000);
    ASSERT(ctlMgr.getActiveCount() == 1);

    PASS();
}

// ---- Test: EmergencyStopChain executes all registered steps ----

void test_estop_chain_full() {
    TEST(estop_chain_executes_all_steps);

    chopper::safety::EmergencyStopChain chain;
    int step_flags[3] = {0, 0, 0};

    chain.registerStep(0, "step0",
        [](const char*, uint8_t, void* ctx) {
            static_cast<int*>(ctx)[0] = 1;
        }, step_flags);

    chain.registerStep(1, "step1",
        [](const char*, uint8_t, void* ctx) {
            static_cast<int*>(ctx)[1] = 1;
        }, step_flags);

    chain.registerStep(2, "step2",
        [](const char*, uint8_t, void* ctx) {
            static_cast<int*>(ctx)[2] = 1;
        }, step_flags);

    ASSERT(!chain.isTriggered());

    chain.execute("test", 0);

    ASSERT(chain.isTriggered());
    ASSERT(step_flags[0] == 1);
    ASSERT(step_flags[1] == 1);
    ASSERT(step_flags[2] == 1);

    // Idempotent — second call should be no-op
    step_flags[0] = 0;
    chain.execute("test again", 0);
    ASSERT(step_flags[0] == 0);  // not re-executed

    // Reset and re-trigger
    chain.reset();
    ASSERT(!chain.isTriggered());
    chain.execute("re-triggered", 0);
    ASSERT(step_flags[0] == 1);

    PASS();
}

// ---- Test: Multiple message types on same topic (type safety) ----

void test_cross_type_safety() {
    TEST(different_message_types_on_same_topic_are_isolated);

    auto& broker = chopper::core::MessageBroker::getInstance();

    // Publisher for MotorCommand on "shared_topic"
    auto motorPub = broker.createPublisher<chopper::messages::MotorCommand>("shared_topic_int");

    // Subscriber for ServoCommand on same topic
    int servoCallCount = 0;
    auto servoSub = broker.createSubscription<chopper::messages::ServoCommand>(
        "shared_topic_int",
        [](const chopper::messages::ServoCommand&, void* ctx) {
            (*static_cast<int*>(ctx))++;
        },
        &servoCallCount);

    // Subscriber for MotorCommand on same topic
    int motorCallCount = 0;
    auto motorSub = broker.createSubscription<chopper::messages::MotorCommand>(
        "shared_topic_int",
        [](const chopper::messages::MotorCommand&, void* ctx) {
            (*static_cast<int*>(ctx))++;
        },
        &motorCallCount);

    // Publish a MotorCommand
    chopper::messages::MotorCommand cmd;
    cmd.motor_id = 1;
    motorPub->publish(cmd);

    // Only the MotorCommand subscriber should have received it
    ASSERT(motorCallCount == 1);
    ASSERT(servoCallCount == 0);

    PASS();
}

// ---- Main ----

int main() {
    printf("=== Chopper Integration Tests ===\n\n");

    test_motor_bridge_e2e();
    test_servo_bridge_e2e();
    test_audio_bridge_e2e();
    test_safety_node_status();
    test_emergency_stop_propagation();
    test_executor_safety_wiring();
    test_controller_disconnect_fallback();
    test_full_pipeline();
    test_timer_integration();
    test_application_init();
    test_application_double_init();
    test_application_motor_limits();
    test_cross_subsystem_safety();
    test_bluetooth_role_assignment();
    test_estop_chain_full();
    test_cross_type_safety();

    printf("\n=== Results: %d/%d passed ===\n", pass_count, test_count);
    return (pass_count == test_count) ? 0 : 1;
}
