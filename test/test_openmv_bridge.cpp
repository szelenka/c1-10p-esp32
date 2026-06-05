// Host-side tests for OpenMvBridgeNode.
//
// Compile:
//   g++ -std=c++20 -I test/mocks -I main/include \
//       test/test_openmv_bridge.cpp \
//       main/chopper/core/Node.cpp \
//       main/chopper/core/Publisher.cpp \
//       main/chopper/core/Subscription.cpp \
//       main/chopper/core/MessageBroker.cpp \
//       main/chopper/core/PublishingNode.cpp \
//       main/chopper/core/Message.cpp \
//       -o test/test_openmv_bridge -pthread

#include <cstdio>
#include <cassert>
#include <cstring>
#include <memory>

#include "chopper/core/Message.h"
#include "chopper/core/Node.h"
#include "chopper/core/Publisher.h"
#include "chopper/core/Subscription.h"
#include "chopper/core/MessageBroker.h"
#include "chopper/core/PublishingNode.h"
#include "chopper/messages/CommonMessages.h"
#include "chopper/hal/ISerialPort.h"
#include "chopper/nodes/OpenMvBridgeNode.h"

// ---- Test helpers ----

static int test_count = 0;
static int pass_count = 0;

#define TEST(name)                                        \
    do {                                                  \
        test_count++;                                     \
        printf("TEST: %s ... ", #name);                   \
    } while (0)
#define PASS()                                            \
    do {                                                  \
        pass_count++;                                     \
        printf("PASS\n");                                 \
    } while (0)
#define ASSERT(cond)                                      \
    do {                                                  \
        if (!(cond)) {                                    \
            printf("FAIL at %s:%d: %s\n", __FILE__,       \
                   __LINE__, #cond);                       \
            return;                                        \
        }                                                  \
    } while (0)

// ---- Mock serial port ----

class MockSerialPort : public chopper::hal::ISerialPort {
public:
    static constexpr size_t BUF_SIZE = 256;
    uint8_t tx_buf[BUF_SIZE] = {};
    size_t tx_len = 0;
    uint8_t rx_buf[BUF_SIZE] = {};
    size_t rx_len = 0;
    size_t rx_pos = 0;

    size_t write(const uint8_t* data, size_t length) override {
        for (size_t i = 0; i < length && tx_len < BUF_SIZE; ++i) {
            tx_buf[tx_len++] = data[i];
        }
        return length;
    }

    int available() override { return static_cast<int>(rx_len - rx_pos); }

    int read() override {
        if (rx_pos >= rx_len) {
            return -1;
        }
        return rx_buf[rx_pos++];
    }

    void clearTx() {
        tx_len = 0;
        std::memset(tx_buf, 0, BUF_SIZE);
    }

    void injectRx(const uint8_t* data, size_t len) {
        rx_pos = 0;
        rx_len = 0;
        for (size_t i = 0; i < len && rx_len < BUF_SIZE; ++i) {
            rx_buf[rx_len++] = data[i];
        }
    }
};

// Helper: compute XOR checksum
static uint8_t xorChecksum(uint8_t cmd, uint8_t len, const uint8_t* payload) {
    uint8_t cs = cmd ^ len;
    for (uint8_t i = 0; i < len; ++i) {
        cs ^= payload[i];
    }
    return cs;
}

// ============================================================
// Tests
// ============================================================

void test_initialize_with_null_serial() {
    TEST(initialize_with_null_serial);
    auto node = std::make_shared<chopper::nodes::OpenMvBridgeNode>(nullptr);
    ASSERT(!node->initialize());
    PASS();
}

void test_initialize_success() {
    TEST(initialize_success);
    MockSerialPort serial;
    auto node = std::make_shared<chopper::nodes::OpenMvBridgeNode>(&serial);
    ASSERT(node->initialize());
    PASS();
}

void test_activate_disables_tracking_and_periscope_led() {
    TEST(activate_disables_tracking_and_periscope_led);
    MockSerialPort serial;
    auto node = std::make_shared<chopper::nodes::OpenMvBridgeNode>(&serial);
    ASSERT(node->initialize());

    ASSERT(node->activate());

    using Node = chopper::nodes::OpenMvBridgeNode;
    ASSERT(serial.tx_len == 10);
    ASSERT(serial.tx_buf[0] == Node::SYNC_BYTE);
    ASSERT(serial.tx_buf[1] == Node::CMD_TRACKING_SET);
    ASSERT(serial.tx_buf[2] == 1);
    ASSERT(serial.tx_buf[3] == 0);
    ASSERT(serial.tx_buf[4] == xorChecksum(Node::CMD_TRACKING_SET, 1, &serial.tx_buf[3]));
    ASSERT(serial.tx_buf[5] == Node::SYNC_BYTE);
    ASSERT(serial.tx_buf[6] == Node::CMD_LED_OFF);
    ASSERT(serial.tx_buf[7] == 1);
    ASSERT(serial.tx_buf[8] == Node::LED_ID_PERISCOPE);
    ASSERT(serial.tx_buf[9] == xorChecksum(Node::CMD_LED_OFF, 1, &serial.tx_buf[8]));
    PASS();
}

void test_led_set_color_sends_frame() {
    TEST(led_set_color_sends_frame);
    MockSerialPort serial;
    auto& broker = chopper::core::MessageBroker::getInstance();
    auto node = std::make_shared<chopper::nodes::OpenMvBridgeNode>(&serial);
    ASSERT(node->initialize());

    auto pub = broker.createPublisher<chopper::messages::LEDCommand>("led/dome_eye/cmd");
    ASSERT(pub != nullptr);

    chopper::messages::LEDCommand cmd;
    cmd.command_type = chopper::messages::LEDCommand::CommandType::SET_COLOR;
    cmd.led_id = 3;
    cmd.color.red = 0xFF;
    cmd.color.green = 0x80;
    cmd.color.blue = 0x40;
    cmd.color.white = 0x00;
    pub->publish(cmd);

    // Frame: SYNC(0xA5), CMD(0x01), LEN(5), led_id, r, g, b, w, CHECKSUM
    using Node = chopper::nodes::OpenMvBridgeNode;
    ASSERT(serial.tx_len == 9);
    ASSERT(serial.tx_buf[0] == Node::SYNC_BYTE);
    ASSERT(serial.tx_buf[1] == Node::CMD_LED_SET_COLOR);
    ASSERT(serial.tx_buf[2] == 5);
    ASSERT(serial.tx_buf[3] == 3);    // led_id
    ASSERT(serial.tx_buf[4] == 0xFF); // red
    ASSERT(serial.tx_buf[5] == 0x80); // green
    ASSERT(serial.tx_buf[6] == 0x40); // blue
    ASSERT(serial.tx_buf[7] == 0x00); // white

    uint8_t payload[] = {3, 0xFF, 0x80, 0x40, 0x00};
    uint8_t expected_cs = xorChecksum(Node::CMD_LED_SET_COLOR, 5, payload);
    ASSERT(serial.tx_buf[8] == expected_cs);
    PASS();
}

void test_led_off_sends_frame() {
    TEST(led_off_sends_frame);
    MockSerialPort serial;
    auto& broker = chopper::core::MessageBroker::getInstance();
    auto node = std::make_shared<chopper::nodes::OpenMvBridgeNode>(&serial);
    ASSERT(node->initialize());

    auto pub = broker.createPublisher<chopper::messages::LEDCommand>("led/dome_eye/cmd");

    chopper::messages::LEDCommand cmd;
    cmd.command_type = chopper::messages::LEDCommand::CommandType::TURN_OFF;
    cmd.led_id = 1;
    pub->publish(cmd);

    using Node = chopper::nodes::OpenMvBridgeNode;
    ASSERT(serial.tx_len == 5);
    ASSERT(serial.tx_buf[0] == Node::SYNC_BYTE);
    ASSERT(serial.tx_buf[1] == Node::CMD_LED_OFF);
    ASSERT(serial.tx_buf[2] == 1); // LEN
    ASSERT(serial.tx_buf[3] == 1); // led_id

    uint8_t payload[] = {1};
    uint8_t expected_cs = xorChecksum(Node::CMD_LED_OFF, 1, payload);
    ASSERT(serial.tx_buf[4] == expected_cs);
    PASS();
}

void test_led_on_sends_frame() {
    TEST(led_on_sends_frame);
    MockSerialPort serial;
    auto& broker = chopper::core::MessageBroker::getInstance();
    auto node = std::make_shared<chopper::nodes::OpenMvBridgeNode>(&serial);
    ASSERT(node->initialize());

    auto pub = broker.createPublisher<chopper::messages::LEDCommand>("led/dome_eye/cmd");

    chopper::messages::LEDCommand cmd;
    cmd.command_type = chopper::messages::LEDCommand::CommandType::TURN_ON;
    cmd.led_id = 2;
    pub->publish(cmd);

    using Node = chopper::nodes::OpenMvBridgeNode;
    ASSERT(serial.tx_len == 5);
    ASSERT(serial.tx_buf[1] == Node::CMD_LED_ON);
    ASSERT(serial.tx_buf[3] == 2);
    PASS();
}

void test_led_set_brightness_sends_frame() {
    TEST(led_set_brightness_sends_frame);
    MockSerialPort serial;
    auto& broker = chopper::core::MessageBroker::getInstance();
    auto node = std::make_shared<chopper::nodes::OpenMvBridgeNode>(&serial);
    ASSERT(node->initialize());

    auto pub = broker.createPublisher<chopper::messages::LEDCommand>("led/dome_eye/cmd");

    chopper::messages::LEDCommand cmd;
    cmd.command_type = chopper::messages::LEDCommand::CommandType::SET_BRIGHTNESS;
    cmd.led_id = 0;
    cmd.brightness = 128;
    pub->publish(cmd);

    using Node = chopper::nodes::OpenMvBridgeNode;
    ASSERT(serial.tx_len == 6);
    ASSERT(serial.tx_buf[1] == Node::CMD_LED_SET_BRIGHTNESS);
    ASSERT(serial.tx_buf[2] == 2);   // LEN
    ASSERT(serial.tx_buf[3] == 0);   // led_id
    ASSERT(serial.tx_buf[4] == 128); // brightness
    PASS();
}

void test_led_set_pattern_sends_frame() {
    TEST(led_set_pattern_sends_frame);
    MockSerialPort serial;
    auto& broker = chopper::core::MessageBroker::getInstance();
    auto node = std::make_shared<chopper::nodes::OpenMvBridgeNode>(&serial);
    ASSERT(node->initialize());

    auto pub = broker.createPublisher<chopper::messages::LEDCommand>("led/dome_eye/cmd");

    chopper::messages::LEDCommand cmd;
    cmd.command_type = chopper::messages::LEDCommand::CommandType::SET_PATTERN;
    cmd.led_id = 5;
    cmd.pattern_id = 7;
    pub->publish(cmd);

    using Node = chopper::nodes::OpenMvBridgeNode;
    ASSERT(serial.tx_len == 6);
    ASSERT(serial.tx_buf[1] == Node::CMD_LED_SET_PATTERN);
    ASSERT(serial.tx_buf[3] == 5); // led_id
    ASSERT(serial.tx_buf[4] == 7); // pattern_id
    PASS();
}

void test_vision_result_parsed() {
    TEST(vision_result_parsed);
    MockSerialPort serial;
    auto& broker = chopper::core::MessageBroker::getInstance();
    auto node = std::make_shared<chopper::nodes::OpenMvBridgeNode>(&serial);
    ASSERT(node->initialize());

    // Subscribe via broker to capture published VisionResult
    int vision_count = 0;
    chopper::messages::VisionResult captured{};
    struct Ctx {
        int* count;
        chopper::messages::VisionResult* result;
    };
    Ctx ctx{&vision_count, &captured};
    auto sub = broker.createSubscription<chopper::messages::VisionResult>(
        "vision/result",
        [](const chopper::messages::VisionResult& msg, void* c) {
            auto* x = static_cast<Ctx*>(c);
            (*x->count)++;
            *x->result = msg;
        },
        &ctx);
    ASSERT(sub != nullptr);

    // Build a valid vision result frame: cx=100, cy=-50, w=80, h=60, confidence=200, detected=1
    using Node = chopper::nodes::OpenMvBridgeNode;
    uint8_t payload[] = {
        0x00, 0x64, // center_x = 100 (big-endian)
        0xFF, 0xCE, // center_y = -50 (big-endian, two's complement)
        0x00, 0x50, // width = 80
        0x00, 0x3C, // height = 60
        200,        // confidence
        1           // detected
    };
    uint8_t frame[] = {
        Node::SYNC_BYTE, Node::CMD_VISION_RESULT, 10,
        payload[0], payload[1], payload[2], payload[3], payload[4],
        payload[5], payload[6], payload[7], payload[8], payload[9],
        xorChecksum(Node::CMD_VISION_RESULT, 10, payload)};
    serial.injectRx(frame, sizeof(frame));

    node->process(0);

    ASSERT(vision_count == 1);
    ASSERT(captured.center_x == 100);
    ASSERT(captured.center_y == -50);
    ASSERT(captured.width == 80);
    ASSERT(captured.height == 60);
    ASSERT(captured.confidence == 200);
    ASSERT(captured.detected == true);
    PASS();
}

void test_vision_result_bad_checksum_ignored() {
    TEST(vision_result_bad_checksum_ignored);
    MockSerialPort serial;
    auto& broker = chopper::core::MessageBroker::getInstance();
    auto node = std::make_shared<chopper::nodes::OpenMvBridgeNode>(&serial);
    ASSERT(node->initialize());

    int vision_count = 0;
    auto sub = broker.createSubscription<chopper::messages::VisionResult>(
        "vision/result",
        [](const chopper::messages::VisionResult&, void* c) { (*static_cast<int*>(c))++; },
        &vision_count);

    using Node = chopper::nodes::OpenMvBridgeNode;
    uint8_t payload[] = {0x00, 0x01, 0x00, 0x02, 0x00, 0x10, 0x00, 0x10, 50, 1};
    uint8_t frame[] = {
        Node::SYNC_BYTE, Node::CMD_VISION_RESULT, 10,
        payload[0], payload[1], payload[2], payload[3], payload[4],
        payload[5], payload[6], payload[7], payload[8], payload[9],
        0xFF}; // wrong checksum
    serial.injectRx(frame, sizeof(frame));

    node->process(0);

    ASSERT(vision_count == 0);
    ASSERT(node->isIdle());
    PASS();
}

void test_parser_recovers_from_garbage() {
    TEST(parser_recovers_from_garbage);
    MockSerialPort serial;
    auto& broker = chopper::core::MessageBroker::getInstance();
    auto node = std::make_shared<chopper::nodes::OpenMvBridgeNode>(&serial);
    ASSERT(node->initialize());

    int vision_count = 0;
    chopper::messages::VisionResult captured{};
    struct Ctx {
        int* count;
        chopper::messages::VisionResult* result;
    };
    Ctx ctx{&vision_count, &captured};
    auto sub = broker.createSubscription<chopper::messages::VisionResult>(
        "vision/result",
        [](const chopper::messages::VisionResult& msg, void* c) {
            auto* x = static_cast<Ctx*>(c);
            (*x->count)++;
            *x->result = msg;
        },
        &ctx);

    using Node = chopper::nodes::OpenMvBridgeNode;
    uint8_t payload[] = {0x00, 0x0A, 0x00, 0x14, 0x00, 0x20, 0x00, 0x18, 100, 1};
    uint8_t valid_cs = xorChecksum(Node::CMD_VISION_RESULT, 10, payload);

    // Garbage bytes followed by a valid frame.
    // The 0xA5 in garbage triggers a false sync, 0x00 becomes cmd,
    // 0x01 becomes len=1, 0x12 becomes payload, then checksum fails
    // and parser resets to WAIT_SYNC in time for the real frame.
    uint8_t data[] = {
        0x12, 0x34, 0xA5, 0x00, 0x01, 0x12, 0xFF, // garbage w/ false sync
        Node::SYNC_BYTE, Node::CMD_VISION_RESULT, 10,
        payload[0], payload[1], payload[2], payload[3], payload[4],
        payload[5], payload[6], payload[7], payload[8], payload[9],
        valid_cs};
    serial.injectRx(data, sizeof(data));

    node->process(0);

    ASSERT(vision_count == 1);
    ASSERT(captured.center_x == 10);
    ASSERT(captured.center_y == 20);
    ASSERT(captured.width == 32);
    ASSERT(captured.height == 24);
    PASS();
}

void test_oversized_payload_rejected() {
    TEST(oversized_payload_rejected);
    MockSerialPort serial;
    auto& broker = chopper::core::MessageBroker::getInstance();
    auto node = std::make_shared<chopper::nodes::OpenMvBridgeNode>(&serial);
    ASSERT(node->initialize());

    int vision_count = 0;
    auto sub = broker.createSubscription<chopper::messages::VisionResult>(
        "vision/result",
        [](const chopper::messages::VisionResult&, void* c) { (*static_cast<int*>(c))++; },
        &vision_count);

    using Node = chopper::nodes::OpenMvBridgeNode;
    // Frame with LEN > MAX_PAYLOAD_SIZE — parser should reset
    uint8_t data[] = {Node::SYNC_BYTE, Node::CMD_VISION_RESULT, Node::MAX_PAYLOAD_SIZE + 1};
    serial.injectRx(data, sizeof(data));

    node->process(0);

    ASSERT(vision_count == 0);
    ASSERT(node->isIdle());
    PASS();
}

void test_emergency_stop_sends_openmv_outputs_off() {
    TEST(emergency_stop_sends_openmv_outputs_off);
    MockSerialPort serial;
    auto node = std::make_shared<chopper::nodes::OpenMvBridgeNode>(&serial);
    ASSERT(node->initialize());

    node->emergencyStop();

    using Node = chopper::nodes::OpenMvBridgeNode;
    ASSERT(serial.tx_len == 15);
    ASSERT(serial.tx_buf[0] == Node::SYNC_BYTE);
    ASSERT(serial.tx_buf[1] == Node::CMD_LED_OFF);
    ASSERT(serial.tx_buf[2] == 1);  // LEN
    ASSERT(serial.tx_buf[3] == Node::LED_ID_RIGHT_EYE);
    ASSERT(serial.tx_buf[4] == xorChecksum(Node::CMD_LED_OFF, 1, &serial.tx_buf[3]));

    ASSERT(serial.tx_buf[5] == Node::SYNC_BYTE);
    ASSERT(serial.tx_buf[6] == Node::CMD_LED_OFF);
    ASSERT(serial.tx_buf[7] == 1);
    ASSERT(serial.tx_buf[8] == Node::LED_ID_CENTER_EYE);
    ASSERT(serial.tx_buf[9] == xorChecksum(Node::CMD_LED_OFF, 1, &serial.tx_buf[8]));

    ASSERT(serial.tx_buf[10] == Node::SYNC_BYTE);
    ASSERT(serial.tx_buf[11] == Node::CMD_LED_OFF);
    ASSERT(serial.tx_buf[12] == 1);
    ASSERT(serial.tx_buf[13] == Node::LED_ID_PERISCOPE);
    ASSERT(serial.tx_buf[14] == xorChecksum(Node::CMD_LED_OFF, 1, &serial.tx_buf[13]));

    PASS();
}

// ============================================================

int main() {
    test_initialize_with_null_serial();
    test_initialize_success();
    test_activate_disables_tracking_and_periscope_led();
    test_led_set_color_sends_frame();
    test_led_off_sends_frame();
    test_led_on_sends_frame();
    test_led_set_brightness_sends_frame();
    test_led_set_pattern_sends_frame();
    test_vision_result_parsed();
    test_vision_result_bad_checksum_ignored();
    test_parser_recovers_from_garbage();
    test_oversized_payload_rejected();
    test_emergency_stop_sends_openmv_outputs_off();

    printf("\n=== Results: %d/%d passed ===\n", pass_count, test_count);
    return (pass_count == test_count) ? 0 : 1;
}
