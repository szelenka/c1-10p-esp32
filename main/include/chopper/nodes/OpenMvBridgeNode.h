#pragma once

#include "chopper/core/PublishingNode.h"
#include "chopper/hal/ISerialPort.h"
#include "chopper/messages/CommonMessages.h"

namespace chopper::nodes {

/**
 * Bridge node for ESP32 ↔ OpenMV serial communication.
 *
 * Subscribes to OpenMV LED and tracking commands, serializes them into
 * a binary wire protocol, and writes to the OpenMV serial port.
 * Reads incoming vision result frames from the OpenMV and publishes
 * VisionResult messages.
 *
 * Wire protocol (both directions):
 *   [SYNC(0xA5), CMD_ID, LEN, PAYLOAD..., CHECKSUM]
 *   CHECKSUM = XOR of CMD_ID ^ LEN ^ each payload byte
 *
 * ESP32 → OpenMV commands:
 *   0x01 LED_SET_COLOR     : led_id, r, g, b, w       (5 bytes)
 *   0x02 LED_SET_BRIGHTNESS: led_id, brightness        (2 bytes)
 *   0x03 LED_SET_PATTERN   : led_id, pattern_id        (2 bytes)
 *   0x04 LED_OFF           : led_id                    (1 byte)
 *   0x05 LED_ON            : led_id                    (1 byte)
 *   0x10 TRACKING_SET      : enabled                   (1 byte)
 *
 * OpenMV → ESP32 commands:
 *   0x80 VISION_RESULT     : cx_hi, cx_lo, cy_hi, cy_lo, w_hi, w_lo, h_hi, h_lo, confidence, detected (10 bytes)
 */
class OpenMvBridgeNode : public core::PublishingNode {
public:
    explicit OpenMvBridgeNode(hal::ISerialPort* serial) : PublishingNode("openmv_bridge"), serial_(serial) {}

    bool initialize() override {
        if (serial_ == nullptr) {
            return false;
        }
        led_sub_ = createSubscription<messages::LEDCommand>("led/dome_eye/cmd", &OpenMvBridgeNode::onLedCommand, this);
        tracking_sub_ = createSubscription<messages::TrackingCommand>("openmv/tracking/cmd",
                                                                      &OpenMvBridgeNode::onTrackingCommand, this);
        vision_pub_ = createPublisher<messages::VisionResult>("vision/result");
        return led_sub_ != nullptr && tracking_sub_ != nullptr && vision_pub_ != nullptr;
    }

    void process(uint64_t) override { readSerial(); }

    void emergencyStop() override {
        sendLedOff(LED_ID_RIGHT_EYE);
        sendLedOff(LED_ID_CENTER_EYE);
        sendLedOff(LED_ID_PERISCOPE);
    }

    // --- Wire protocol constants (public for tests and MicroPython reference) ---
    static constexpr uint8_t SYNC_BYTE = 0xA5;

    // ESP32 → OpenMV
    static constexpr uint8_t CMD_LED_SET_COLOR = 0x01;
    static constexpr uint8_t CMD_LED_SET_BRIGHTNESS = 0x02;
    static constexpr uint8_t CMD_LED_SET_PATTERN = 0x03;
    static constexpr uint8_t CMD_LED_OFF = 0x04;
    static constexpr uint8_t CMD_LED_ON = 0x05;
    static constexpr uint8_t CMD_TRACKING_SET = 0x10;

    // OpenMV → ESP32
    static constexpr uint8_t CMD_VISION_RESULT = 0x80;

    static constexpr uint8_t MAX_PAYLOAD_SIZE = 16;

    static constexpr uint8_t LED_ID_RIGHT_EYE = 1;
    static constexpr uint8_t LED_ID_CENTER_EYE = 2;
    static constexpr uint8_t LED_ID_PERISCOPE = 4;

    /// Expose parse state for testing.
    [[nodiscard]] bool isIdle() const { return parse_state_ == ParseState::WAIT_SYNC; }

protected:
    bool onActivate() override {
        sendTrackingSet(false);
        sendLedOff(LED_ID_PERISCOPE);
        return true;
    }

private:
    // ---- TX: pub/sub → serial ----

    void onLedCommand(const messages::LEDCommand& cmd) {
        switch (cmd.command_type) {
            case messages::LEDCommand::CommandType::SET_COLOR: {
                uint8_t payload[] = {cmd.led_id, cmd.color.red, cmd.color.green, cmd.color.blue, cmd.color.white};
                sendFrame(CMD_LED_SET_COLOR, payload, sizeof(payload));
                break;
            }
            case messages::LEDCommand::CommandType::SET_BRIGHTNESS: {
                uint8_t payload[] = {cmd.led_id, cmd.brightness};
                sendFrame(CMD_LED_SET_BRIGHTNESS, payload, sizeof(payload));
                break;
            }
            case messages::LEDCommand::CommandType::SET_PATTERN: {
                uint8_t payload[] = {cmd.led_id, cmd.pattern_id};
                sendFrame(CMD_LED_SET_PATTERN, payload, sizeof(payload));
                break;
            }
            case messages::LEDCommand::CommandType::TURN_OFF: {
                uint8_t payload[] = {cmd.led_id};
                sendFrame(CMD_LED_OFF, payload, sizeof(payload));
                break;
            }
            case messages::LEDCommand::CommandType::TURN_ON: {
                uint8_t payload[] = {cmd.led_id};
                sendFrame(CMD_LED_ON, payload, sizeof(payload));
                break;
            }
        }
    }

    void onTrackingCommand(const messages::TrackingCommand& cmd) { sendTrackingSet(cmd.enabled); }

    void sendLedOff(uint8_t led_id) {
        uint8_t payload[] = {led_id};
        sendFrame(CMD_LED_OFF, payload, sizeof(payload));
    }

    void sendTrackingSet(bool enabled) {
        uint8_t payload[] = {static_cast<uint8_t>(enabled ? 1 : 0)};
        sendFrame(CMD_TRACKING_SET, payload, sizeof(payload));
    }

    void sendFrame(uint8_t cmd_id, const uint8_t* payload, uint8_t len) {
        if (serial_ == nullptr || len > MAX_PAYLOAD_SIZE) {
            return;
        }
        // Frame: SYNC + CMD + LEN + payload + CHECKSUM
        static constexpr uint8_t HEADER_SIZE = 3;
        uint8_t frame[HEADER_SIZE + MAX_PAYLOAD_SIZE + 1];
        frame[0] = SYNC_BYTE;
        frame[1] = cmd_id;
        frame[2] = len;
        uint8_t checksum = cmd_id ^ len;
        for (uint8_t i = 0; i < len; ++i) {
            frame[HEADER_SIZE + i] = payload[i];
            checksum ^= payload[i];
        }
        frame[HEADER_SIZE + len] = checksum;
        serial_->write(frame, static_cast<size_t>(HEADER_SIZE + len + 1));
    }

    // ---- RX: serial → pub/sub ----

    void readSerial() {
        while (serial_->available() > 0) {
            const int byte = serial_->read();
            if (byte < 0) {
                break;
            }
            feedByte(static_cast<uint8_t>(byte));
        }
    }

    enum class ParseState : uint8_t {
        WAIT_SYNC,
        WAIT_CMD,
        WAIT_LEN,
        WAIT_PAYLOAD,
        WAIT_CHECKSUM
    };

    void feedByte(uint8_t byte) {
        switch (parse_state_) {
            case ParseState::WAIT_SYNC:
                if (byte == SYNC_BYTE) {
                    parse_state_ = ParseState::WAIT_CMD;
                }
                break;
            case ParseState::WAIT_CMD:
                rx_cmd_ = byte;
                parse_state_ = ParseState::WAIT_LEN;
                break;
            case ParseState::WAIT_LEN:
                rx_len_ = byte;
                rx_idx_ = 0;
                if (rx_len_ == 0) {
                    parse_state_ = ParseState::WAIT_CHECKSUM;
                } else if (rx_len_ > MAX_PAYLOAD_SIZE) {
                    parse_state_ = ParseState::WAIT_SYNC;
                } else {
                    parse_state_ = ParseState::WAIT_PAYLOAD;
                }
                break;
            case ParseState::WAIT_PAYLOAD:
                rx_buf_[rx_idx_++] = byte;
                if (rx_idx_ >= rx_len_) {
                    parse_state_ = ParseState::WAIT_CHECKSUM;
                }
                break;
            case ParseState::WAIT_CHECKSUM: {
                uint8_t expected = rx_cmd_ ^ rx_len_;
                for (uint8_t i = 0; i < rx_len_; ++i) {
                    expected ^= rx_buf_[i];
                }
                if (byte == expected) {
                    handleFrame(rx_cmd_, rx_buf_, rx_len_);
                }
                parse_state_ = ParseState::WAIT_SYNC;
                break;
            }
        }
    }

    void handleFrame(uint8_t cmd, const uint8_t* payload, uint8_t len) {
        if (cmd == CMD_VISION_RESULT && len >= 10) {
            messages::VisionResult result;
            result.center_x = static_cast<int16_t>(static_cast<uint16_t>((payload[0] << 8) | payload[1]));
            result.center_y = static_cast<int16_t>(static_cast<uint16_t>((payload[2] << 8) | payload[3]));
            result.width = static_cast<uint16_t>((payload[4] << 8) | payload[5]);
            result.height = static_cast<uint16_t>((payload[6] << 8) | payload[7]);
            result.confidence = payload[8];
            result.detected = payload[9] != 0;
            if (vision_pub_) {
                vision_pub_->publish(result);
            }
        }
    }

    hal::ISerialPort* serial_;
    core::TypedSubscriptionPtr<messages::LEDCommand> led_sub_;
    core::TypedSubscriptionPtr<messages::TrackingCommand> tracking_sub_;
    core::TypedPublisherPtr<messages::VisionResult> vision_pub_;

    // RX parser state
    ParseState parse_state_ = ParseState::WAIT_SYNC;
    uint8_t rx_cmd_ = 0;
    uint8_t rx_len_ = 0;
    uint8_t rx_idx_ = 0;
    uint8_t rx_buf_[MAX_PAYLOAD_SIZE] = {};
};

}  // namespace chopper::nodes
