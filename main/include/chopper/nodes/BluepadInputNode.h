#pragma once

#ifdef ESP_PLATFORM

#include "chopper/core/PublishingNode.h"
#include "chopper/bluetooth/ControllerManager.h"
#include "chopper/hal/ChopperBluetooth.h"
#include "chopper/messages/CommonMessages.h"

#include "controller/uni_gamepad.h"
#include "esp_log.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace chopper {
namespace nodes {

class BluepadInputNode : public core::PublishingNode {
public:
    explicit BluepadInputNode(bluetooth::ControllerManager* controller_manager,
                              bool trace_input = true,
                              double hz = 50.0)
        : PublishingNode("bluepad_input")
        , controller_manager_(controller_manager)
        , trace_input_(trace_input)
        , hz_(hz)
    {
        for (int i = 0; i < CHOPPER_BT_MAX_DEVICES; ++i) {
            observed_connected_[i] = false;
            mapped_slot_[i] = -1;
            last_buttons_[i] = 0;
            last_misc_[i] = 0;
            last_dpad_[i] = 0;
            last_report_seen_us_[i] = 0;
            have_last_input_[i] = false;
        }
        // Controller join/reconnect can transiently take tens of milliseconds.
        // Keep generous headroom to avoid timeout-triggered emergencyStop on
        // this input-multiplexing node during BT setup bursts.
        setMaxExecutionTime(50000);
    }

    bool initialize() override {
        drive_pub_ = createPublisher<messages::ControllerInput>("controller/drive");
        dome_pub_ = createPublisher<messages::ControllerInput>("controller/dome");
        animation_pub_ = createPublisher<messages::ControllerInput>("controller/animation");
        camera_pub_ = createPublisher<messages::ControllerInput>("controller/camera");
        return controller_manager_ && drive_pub_ && dome_pub_ && animation_pub_ && camera_pub_;
    }

    void process(uint64_t now_us) override {
        if (!controller_manager_) {
            return;
        }

        const uint64_t now_ms = now_us / 1000ULL;

        for (int bt_slot = 0; bt_slot < CHOPPER_BT_MAX_DEVICES; ++bt_slot) {
            chopper_gamepad_data_t data{};
            if (chopper_bt_get_gamepad(bt_slot, &data) != 0) {
                continue;
            }

            handleConnectionTransitions(bt_slot, data, now_ms);
            if (!data.connected) {
                continue;
            }

            const bool has_fresh_report =
                (data.last_report_time_us > 0) &&
                (data.last_report_time_us != last_report_seen_us_[bt_slot]);
            const int8_t slot = mapped_slot_[bt_slot];
            if (has_fresh_report) {
                last_report_seen_us_[bt_slot] = data.last_report_time_us;
                last_input_cache_[bt_slot] = fromGamepad(bt_slot, data);
                have_last_input_[bt_slot] = true;
            }

            if (slot >= 0 && slot < bluetooth::ControllerManager::kMaxSlots && have_last_input_[bt_slot]) {
                // Use BT-report timestamp (not executor loop time) so per-controller
                // watchdog freshness reflects actual packet cadence.
                if (has_fresh_report) {
                    const uint64_t report_ms = data.last_report_time_us / 1000ULL;
                    controller_manager_->recordInput(static_cast<uint8_t>(slot), report_ms, last_input_cache_[bt_slot]);

                    const auto role = controller_manager_->getSlot(static_cast<uint8_t>(slot)).role;
                    publishByRole(role, last_input_cache_[bt_slot]);
                }
            }
        }

        controller_manager_->update(now_ms);

        for (uint8_t slot = 0; slot < bluetooth::ControllerManager::kMaxSlots; ++slot) {
            messages::ControllerInput fallback{};
            if (!controller_manager_->getFallbackInput(slot, fallback)) {
                continue;
            }
            const auto role = controller_manager_->getSlot(slot).role;
            publishByRole(role, fallback);
        }
    }

    void emergencyStop() override {
        messages::ControllerInput zero{};
        publishByRole(bluetooth::ControllerRole::DRIVE, zero);
        publishByRole(bluetooth::ControllerRole::DOME, zero);
        publishByRole(bluetooth::ControllerRole::ANIMATION, zero);
        publishByRole(bluetooth::ControllerRole::CAMERA, zero);
    }

    double getUpdateFrequency() const override { return hz_; }

private:
    static constexpr const char* TAG = "BluepadInput";

    static float normalizeAxis(int32_t v) {
        constexpr float kRange = 512.0f;
        float out = static_cast<float>(v) / kRange;
        if (out > 1.0f) out = 1.0f;
        if (out < -1.0f) out = -1.0f;
        return out;
    }

    static void formatMac(const uint8_t btaddr[6], char out[18]) {
        std::snprintf(out, 18, "%02X:%02X:%02X:%02X:%02X:%02X",
                      btaddr[0], btaddr[1], btaddr[2],
                      btaddr[3], btaddr[4], btaddr[5]);
    }

    void handleConnectionTransitions(int bt_slot,
                                     const chopper_gamepad_data_t& data,
                                     uint64_t now_ms) {
        if (data.connected && !observed_connected_[bt_slot]) {
            bluetooth::MacAddress mac{};
            std::memcpy(mac.addr, data.btaddr, sizeof(mac.addr));
            mapped_slot_[bt_slot] =
                controller_manager_->onConnect(mac, data.controller_type, 0, 0, now_ms);
            observed_connected_[bt_slot] = true;
        } else if (!data.connected && observed_connected_[bt_slot]) {
            if (mapped_slot_[bt_slot] >= 0) {
                controller_manager_->onDisconnect(
                    static_cast<uint8_t>(mapped_slot_[bt_slot]), now_ms);
            }
            observed_connected_[bt_slot] = false;
            mapped_slot_[bt_slot] = -1;
            last_report_seen_us_[bt_slot] = 0;
            have_last_input_[bt_slot] = false;
        }
    }

    messages::ControllerInput fromGamepad(int bt_slot,
                                          const chopper_gamepad_data_t& data) {
        messages::ControllerInput out{};
        out.dpad = data.gamepad.dpad;
        out.axis_x = data.gamepad.axis_x;
        out.axis_y = data.gamepad.axis_y;
        out.axis_rx = data.gamepad.axis_rx;
        out.axis_ry = data.gamepad.axis_ry;
        out.brake = data.gamepad.brake;
        out.throttle = data.gamepad.throttle;
        out.buttons = data.gamepad.buttons;
        out.misc_buttons = data.gamepad.misc_buttons;
        out.gyro_x = data.gamepad.gyro[0];
        out.gyro_y = data.gamepad.gyro[1];
        out.gyro_z = data.gamepad.gyro[2];
        out.accel_x = data.gamepad.accel[0];
        out.accel_y = data.gamepad.accel[1];
        out.accel_z = data.gamepad.accel[2];

        out.button_a = (out.buttons & BUTTON_A) != 0;
        out.button_b = (out.buttons & BUTTON_B) != 0;
        out.button_x = (out.buttons & BUTTON_X) != 0;
        out.button_y = (out.buttons & BUTTON_Y) != 0;
        out.button_l1 = (out.buttons & BUTTON_SHOULDER_L) != 0;
        out.button_l2 = (out.buttons & BUTTON_TRIGGER_L) != 0;
        out.button_r1 = (out.buttons & BUTTON_SHOULDER_R) != 0;
        out.button_r2 = (out.buttons & BUTTON_TRIGGER_R) != 0;
        out.button_thumb_l = (out.buttons & BUTTON_THUMB_L) != 0;
        out.button_thumb_r = (out.buttons & BUTTON_THUMB_R) != 0;
        out.misc_system = (out.misc_buttons & MISC_BUTTON_SYSTEM) != 0;
        out.misc_select = (out.misc_buttons & MISC_BUTTON_SELECT) != 0;
        out.misc_start = (out.misc_buttons & MISC_BUTTON_START) != 0;
        out.misc_capture = (out.misc_buttons & MISC_BUTTON_CAPTURE) != 0;

        out.controller_id = static_cast<uint8_t>(bt_slot);
        out.battery_level = data.battery;
        out.is_connected = data.connected;
        out.has_data = data.connected;
        out.is_gamepad = true;
        formatMac(data.btaddr, out.mac_address);
        out.axis_x_normalized = normalizeAxis(out.axis_x);
        out.axis_y_normalized = normalizeAxis(out.axis_y);
        out.axis_rx_normalized = normalizeAxis(out.axis_rx);
        out.axis_ry_normalized = normalizeAxis(out.axis_ry);
        // Runtime currently has no separate slew-filter node; feed DriveNode
        // with normalized axes so input maps to motor commands as expected.
        out.axis_x_slew = out.axis_x_normalized;
        out.axis_y_slew = out.axis_y_normalized;

        if (trace_input_ &&
            (last_buttons_[bt_slot] != out.buttons ||
             last_misc_[bt_slot] != out.misc_buttons ||
             last_dpad_[bt_slot] != out.dpad)) {
            ESP_LOGI(TAG,
                     "slot=%d mac=%s dpad=0x%02x buttons=0x%04x misc=0x%02x axes=(%ld,%ld,%ld,%ld)",
                     bt_slot, out.mac_address, out.dpad,
                     static_cast<unsigned>(out.buttons),
                     static_cast<unsigned>(out.misc_buttons),
                     static_cast<long>(out.axis_x),
                     static_cast<long>(out.axis_y),
                     static_cast<long>(out.axis_rx),
                     static_cast<long>(out.axis_ry));
            last_buttons_[bt_slot] = out.buttons;
            last_misc_[bt_slot] = out.misc_buttons;
            last_dpad_[bt_slot] = out.dpad;
        }

        return out;
    }

    void publishByRole(bluetooth::ControllerRole role,
                       const messages::ControllerInput& input) {
        switch (role) {
            case bluetooth::ControllerRole::DRIVE:
                if (drive_pub_) drive_pub_->publish(input);
                break;
            case bluetooth::ControllerRole::DOME:
                if (dome_pub_) dome_pub_->publish(input);
                break;
            case bluetooth::ControllerRole::ANIMATION:
                if (animation_pub_) animation_pub_->publish(input);
                break;
            case bluetooth::ControllerRole::CAMERA:
                if (camera_pub_) camera_pub_->publish(input);
                break;
            case bluetooth::ControllerRole::UNASSIGNED:
                break;
        }
    }

    bluetooth::ControllerManager* controller_manager_;
    bool trace_input_;
    double hz_;

    core::TypedPublisherPtr<messages::ControllerInput> drive_pub_;
    core::TypedPublisherPtr<messages::ControllerInput> dome_pub_;
    core::TypedPublisherPtr<messages::ControllerInput> animation_pub_;
    core::TypedPublisherPtr<messages::ControllerInput> camera_pub_;

    bool observed_connected_[CHOPPER_BT_MAX_DEVICES];
    int8_t mapped_slot_[CHOPPER_BT_MAX_DEVICES];
    uint16_t last_buttons_[CHOPPER_BT_MAX_DEVICES];
    uint8_t last_misc_[CHOPPER_BT_MAX_DEVICES];
    uint8_t last_dpad_[CHOPPER_BT_MAX_DEVICES];
    uint64_t last_report_seen_us_[CHOPPER_BT_MAX_DEVICES];
    messages::ControllerInput last_input_cache_[CHOPPER_BT_MAX_DEVICES];
    bool have_last_input_[CHOPPER_BT_MAX_DEVICES];
};

} // namespace nodes
} // namespace chopper

#endif // ESP_PLATFORM
