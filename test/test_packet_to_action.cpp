// Host-side tests for intent-based packet-to-action behavior.
//
// Compile:
//   g++ -std=c++20 -I test/mocks -I main/include \
//       test/test_packet_to_action.cpp \
//       main/chopper/core/Node.cpp \
//       main/chopper/core/Publisher.cpp \
//       main/chopper/core/Subscription.cpp \
//       main/chopper/core/MessageBroker.cpp \
//       main/chopper/core/PublishingNode.cpp \
//       main/chopper/core/Message.cpp \
//       main/chopper/core/ParameterServer.cpp \
//       -o test/test_packet_to_action -pthread

#include <cstdio>
#include <cmath>
#include <memory>

#include "chopper/core/MessageBroker.h"
#include "chopper/core/ParameterServer.h"
#include "chopper/messages/CommonMessages.h"
#include "chopper/config/HardwareConfig.h"
#include "chopper/nodes/PeriscopeNode.h"

static int test_count = 0;
static int pass_count = 0;

#define TEST(name) \
    do { test_count++; std::printf("TEST: %s ... ", #name); } while (0)
#define PASS() \
    do { pass_count++; std::printf("PASS\n"); } while (0)
#define ASSERT(cond) \
    do { if (!(cond)) { std::printf("FAIL at %s:%d: %s\n", __FILE__, __LINE__, #cond); return; } } while (0)
#define ASSERT_NEAR(a, b, tol) \
    ASSERT(std::fabs((a) - (b)) < (tol))

static void resetFramework() {
    chopper::core::ParameterServer::getInstance().reset();
}

struct ServoCapture {
    int count = 0;
    uint8_t last_servo_id = 255;
    chopper::messages::ServoCommand::CommandType last_type =
        chopper::messages::ServoCommand::CommandType::ENABLE;
    float last_value = 0.0f;
    bool saw_spin = false;
};

void test_periscope_up_intent_publishes_lift() {
    TEST(periscope_up_intent_publishes_lift);
    resetFramework();

    auto node = std::make_shared<chopper::nodes::PeriscopeNode>();
    ASSERT(node->initialize());
    node->activate();

    ServoCapture capture{};
    auto& broker = chopper::core::MessageBroker::getInstance();
    auto sub = broker.createSubscription<chopper::messages::ServoCommand>(
        "servo/dome/cmd",
        [](const chopper::messages::ServoCommand& cmd, void* ctx) {
            auto* c = static_cast<ServoCapture*>(ctx);
            c->count++;
            c->last_servo_id = cmd.servo_id;
            c->last_type = cmd.command_type;
            c->last_value = cmd.value;
            if (cmd.servo_id == chopper::config::servo_channel::DOME_PERISCOPE_SPIN) {
                c->saw_spin = true;
            }
        },
        &capture);

    auto pub = broker.createPublisher<chopper::messages::ControllerInput>("controller/drive");

    chopper::messages::ControllerInput input{};
    input.has_intents = true;
    input.intent_periscope_up = true;
    pub->publish(input);

    ASSERT(capture.count == 1);
    ASSERT(capture.last_servo_id == chopper::config::servo_channel::DOME_PERISCOPE_LIFT);
    ASSERT(capture.last_type == chopper::messages::ServoCommand::CommandType::SET_POSITION);

    int32_t lift_max = 2500;
    chopper::core::ParameterServer::getInstance().get("servo.peri_lift.max", lift_max);
    ASSERT_NEAR(capture.last_value, static_cast<float>(lift_max), 0.1f);
    ASSERT(!capture.saw_spin);
    PASS();
}

void test_periscope_up_then_down_intent() {
    TEST(periscope_up_then_down_intent);
    resetFramework();

    auto node = std::make_shared<chopper::nodes::PeriscopeNode>();
    ASSERT(node->initialize());
    node->activate();

    int lift_count = 0;
    float last_lift_value = 0.0f;

    auto& broker = chopper::core::MessageBroker::getInstance();
    auto sub = broker.createSubscription<chopper::messages::ServoCommand>(
        "servo/dome/cmd",
        [](const chopper::messages::ServoCommand& cmd, void* ctx) {
            auto* last = static_cast<float*>(ctx);
            if (cmd.servo_id == chopper::config::servo_channel::DOME_PERISCOPE_LIFT) {
                *last = cmd.value;
            }
        },
        &last_lift_value);

    auto count_sub = broker.createSubscription<chopper::messages::ServoCommand>(
        "servo/dome/cmd",
        [](const chopper::messages::ServoCommand& cmd, void* ctx) {
            auto* count = static_cast<int*>(ctx);
            if (cmd.servo_id == chopper::config::servo_channel::DOME_PERISCOPE_LIFT) {
                (*count)++;
            }
        },
        &lift_count);

    auto pub = broker.createPublisher<chopper::messages::ControllerInput>("controller/drive");
    chopper::messages::ControllerInput input{};
    input.has_intents = true;

    input.intent_periscope_up = true;
    pub->publish(input);
    input.intent_periscope_up = false;
    pub->publish(input);
    input.intent_periscope_down = true;
    pub->publish(input);

    ASSERT(lift_count == 2);
    int32_t lift_min = 500;
    chopper::core::ParameterServer::getInstance().get("servo.peri_lift.min", lift_min);
    ASSERT_NEAR(last_lift_value, static_cast<float>(lift_min), 0.1f);
    PASS();
}

int main() {
    std::printf("=== Packet To Action Tests ===\n");
    test_periscope_up_intent_publishes_lift();
    test_periscope_up_then_down_intent();

    std::printf("=== Results: %d/%d passed ===\n", pass_count, test_count);
    return (pass_count == test_count) ? 0 : 1;
}
