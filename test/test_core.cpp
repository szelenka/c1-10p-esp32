// Host-side compilation and basic functionality test for the core framework.
// Compile: g++ -std=c++20 -I test/mocks -I main/include test/test_core.cpp \
//          main/chopper/core/Node.cpp main/chopper/core/Publisher.cpp \
//          main/chopper/core/Subscription.cpp main/chopper/core/MessageBroker.cpp \
//          main/chopper/core/PublishingNode.cpp main/chopper/core/Message.cpp \
//          -o test/test_core -pthread

#include <cstdio>
#include <cassert>
#include <cstring>

#include "chopper/chopper_limits.h"
#include "chopper/core/Message.h"
#include "chopper/core/Node.h"
#include "chopper/core/Publisher.h"
#include "chopper/core/Subscription.h"
#include "chopper/core/MessageBroker.h"
#include "chopper/core/PublishingNode.h"
#include "chopper/messages/CommonMessages.h"

// ---- Test helpers ----

static int test_count = 0;
static int pass_count = 0;

#define TEST(name) \
    do { test_count++; printf("TEST: %s ... ", #name); } while(0)
#define PASS() \
    do { pass_count++; printf("PASS\n"); } while(0)
#define ASSERT(cond) \
    do { if (!(cond)) { printf("FAIL at %s:%d: %s\n", __FILE__, __LINE__, #cond); return; } } while(0)

// ---- Test: Type ID system ----

void test_type_ids() {
    TEST(type_ids_are_unique);

    auto id1 = chopper::core::getTypeId<chopper::messages::ControllerInput>();
    auto id2 = chopper::core::getTypeId<chopper::messages::MotorCommand>();
    auto id3 = chopper::core::getTypeId<chopper::messages::SensorData>();

    ASSERT(id1 != id2);
    ASSERT(id1 != id3);
    ASSERT(id2 != id3);

    // Same type should give same ID
    auto id1b = chopper::core::getTypeId<chopper::messages::ControllerInput>();
    ASSERT(id1 == id1b);

    PASS();
}

// ---- Test: Message type ----

void test_typed_message() {
    TEST(typed_message_basics);

    chopper::messages::MotorCommand cmd;
    cmd.motor_id = 1;
    cmd.command_type = chopper::messages::MotorCommand::CommandType::SET_SPEED;
    cmd.value = 0.5f;
    cmd.setTimestamp(12345);

    ASSERT(cmd.getTimestamp() == 12345);
    ASSERT(cmd.getSize() == sizeof(chopper::messages::MotorCommand));
    ASSERT(cmd.getTypeId() == chopper::core::getTypeId<chopper::messages::MotorCommand>());

    PASS();
}

// ---- Test: Node ----

class TestNode : public chopper::core::PublishingNode {
public:
    TestNode() : PublishingNode("test_node"), process_count_(0) {}

    bool initialize() override {
        motor_pub_ = createPublisher<chopper::messages::MotorCommand>("drive/cmd");
        return motor_pub_ != nullptr;
    }

    void process(uint64_t now) override {
        process_count_++;

        // Publish a motor command — zero-allocation path
        chopper::messages::MotorCommand cmd;
        cmd.motor_id = 1;
        cmd.value = 0.75f;
        motor_pub_->publish(cmd, now);
    }

    void emergencyStop() override {
        chopper::messages::MotorCommand stop_cmd;
        stop_cmd.command_type = chopper::messages::MotorCommand::CommandType::EMERGENCY_STOP;
        motor_pub_->publish(stop_cmd);
    }

    double getUpdateFrequency() const override { return 50.0; }

    int process_count_;
    chopper::core::TypedPublisherPtr<chopper::messages::MotorCommand> motor_pub_;
};

class SubscriberNode : public chopper::core::PublishingNode {
public:
    SubscriberNode() : PublishingNode("subscriber_node"), received_count_(0), last_value_(0.0f) {}

    bool initialize() override {
        motor_sub_ = createSubscription<chopper::messages::MotorCommand>(
            "drive/cmd",
            &SubscriberNode::onMotorCommand,
            this
        );
        return motor_sub_ != nullptr;
    }

    void process(uint64_t) override {}
    void emergencyStop() override {}

    void onMotorCommand(const chopper::messages::MotorCommand& cmd) {
        received_count_++;
        last_value_ = cmd.value;
    }

    int received_count_;
    float last_value_;
    chopper::core::TypedSubscriptionPtr<chopper::messages::MotorCommand> motor_sub_;
};

void test_node_lifecycle() {
    TEST(node_lifecycle);

    TestNode node;
    ASSERT(strcmp(node.getName(), "test_node") == 0);
    ASSERT(node.getState() == chopper::core::Node::State::INACTIVE);

    ASSERT(node.initialize());
    ASSERT(node.activate());
    ASSERT(node.getState() == chopper::core::Node::State::ACTIVE);

    ASSERT(node.deactivate());
    ASSERT(node.getState() == chopper::core::Node::State::INACTIVE);

    PASS();
}

void test_pubsub_delivery() {
    TEST(pubsub_zero_alloc_delivery);

    // Create publisher node and subscriber node
    auto pub_node = std::make_shared<TestNode>();
    auto sub_node = std::make_shared<SubscriberNode>();

    // Initialize both (registers with MessageBroker)
    ASSERT(pub_node->initialize());
    ASSERT(sub_node->initialize());

    // Process the publisher — should deliver to subscriber synchronously
    pub_node->activate();
    pub_node->process(1000);

    ASSERT(sub_node->received_count_ == 1);
    ASSERT(sub_node->last_value_ == 0.75f);

    // Process again
    pub_node->process(2000);
    ASSERT(sub_node->received_count_ == 2);

    PASS();
}

void test_type_safety() {
    TEST(type_safety_mismatch_ignored);

    // Create a publisher for MotorCommand and a subscriber for SensorData
    auto& broker = chopper::core::MessageBroker::getInstance();

    auto pub = broker.createPublisher<chopper::messages::MotorCommand>("mixed_topic");

    int callback_count = 0;
    auto sub = broker.createSubscription<chopper::messages::SensorData>(
        "mixed_topic",
        [](const chopper::messages::SensorData&, void* ctx) {
            (*(int*)ctx)++;
        },
        &callback_count
    );

    // Publish a MotorCommand — SensorData subscriber should NOT receive it
    // because TypeId won't match
    chopper::messages::MotorCommand cmd;
    pub->publish(cmd);

    ASSERT(callback_count == 0);

    PASS();
}

void test_qos_profiles() {
    TEST(qos_profiles);

    auto& def = chopper::core::QoSProfile::systemDefault();
    ASSERT(def.reliability == chopper::core::QoSProfile::Reliability::RELIABLE);

    auto& sensor = chopper::core::QoSProfile::sensorData();
    ASSERT(sensor.reliability == chopper::core::QoSProfile::Reliability::BEST_EFFORT);

    auto& cmd = chopper::core::QoSProfile::commandAndControl();
    ASSERT(cmd.reliability == chopper::core::QoSProfile::Reliability::RELIABLE);
    ASSERT(cmd.depth == 5);

    PASS();
}

void test_message_types() {
    TEST(common_message_types);

    // ControllerInput
    chopper::messages::ControllerInput ci;
    ASSERT(ci.axis_x == 0);
    ASSERT(ci.is_connected == false);
    ASSERT(ci.getSize() == sizeof(chopper::messages::ControllerInput));

    // MotorCommand
    chopper::messages::MotorCommand mc(1, chopper::messages::MotorCommand::CommandType::SET_SPEED, 0.5f);
    ASSERT(mc.motor_id == 1);
    ASSERT(mc.value == 0.5f);

    // SystemStatus
    chopper::messages::SystemStatus ss(chopper::messages::SystemStatus::Status::RUNNING);
    ASSERT(ss.system_status == chopper::messages::SystemStatus::Status::RUNNING);

    PASS();
}

// ---- Main ----

int main() {
    printf("=== Chopper Core Framework Tests ===\n\n");

    test_type_ids();
    test_typed_message();
    test_node_lifecycle();
    test_pubsub_delivery();
    test_type_safety();
    test_qos_profiles();
    test_message_types();

    printf("\n=== Results: %d/%d passed ===\n", pass_count, test_count);
    return (pass_count == test_count) ? 0 : 1;
}
