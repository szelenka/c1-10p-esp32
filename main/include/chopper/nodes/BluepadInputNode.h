#pragma once

#ifdef ESP_PLATFORM

#include "chopper/core/PublishingNode.h"
#include "chopper/core/ParameterServer.h"
#include "chopper/bluetooth/ControllerManager.h"
#include "chopper/hal/ChopperBluetooth.h"
#include "chopper/input/ControllerCalibration.h"
#include "chopper/input/DriveIntentMapping.h"
#include "chopper/input/TEmbedInputAdapter.h"
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
    explicit BluepadInputNode(bluetooth::ControllerManager* controller_manager, bool trace_input = true,
                              double hz = 50.0)
        : PublishingNode("bluepad_input"), controller_manager_(controller_manager), trace_input_(trace_input), hz_(hz) {
        for (int i = 0; i < CHOPPER_BT_MAX_DEVICES; ++i) {
            observed_connected_[i] = false;
            mapped_slot_[i] = -1;
            last_buttons_[i] = 0;
            last_misc_[i] = 0;
            last_dpad_[i] = 0;
            last_report_seen_us_[i] = 0;
            last_report_gap_us_[i] = 0;
            last_poll_gap_us_[i] = 0;
            last_report_count_seen_[i] = 0;
            last_reports_since_poll_[i] = 0;
            connect_time_ms_[i] = 0;
            have_last_input_[i] = false;
            split_tembed_[i] = false;
            source_local_stop_count_[i] = 0;
            next_tembed_gap_log_ms_[i] = 0;
            last_tembed_buttons_[i] = 0;
            last_tembed_misc_[i] = 0;
            last_tembed_dpad_[i] = 0;
        }
        tembed_discovery_retry_enabled_ = false;
        next_tembed_discovery_retry_ms_ = 0;
        // Controller join/reconnect can transiently take >100 ms when BT task
        // transitions overlap with logging and slot/role synchronization.
        // Keep enough headroom so single connect bursts warn but do not trip
        // the executor's 2x timeout-to-estop threshold.
        setMaxExecutionTime(80000);
    }

    bool initialize() override {
        drive_pub_ = createPublisher<messages::ControllerInput>("controller/drive");
        dome_pub_ = createPublisher<messages::ControllerInput>("controller/dome");
        animation_pub_ = createPublisher<messages::ControllerInput>("controller/animation");
        camera_pub_ = createPublisher<messages::ControllerInput>("controller/camera");
        return controller_manager_ && drive_pub_ && dome_pub_ && animation_pub_ && camera_pub_ &&
               refreshControllerCalibration();
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

            const bool has_fresh_report =
                (data.last_report_time_us > 0) && (data.last_report_time_us != last_report_seen_us_[bt_slot]);

            if (data.connected && observed_connected_[bt_slot] && has_fresh_report &&
                !mappedSlotStillActive(bt_slot, data)) {
                ESP_LOGW(TAG, "Fresh report for stale controller mapping: bt_slot=%d mapped_slot=%d", bt_slot,
                         static_cast<int>(mapped_slot_[bt_slot]));
                resetObservedSlot(bt_slot);
            }

            handleConnectionTransitions(bt_slot, data, has_fresh_report, now_ms);
            if (!data.connected) {
                continue;
            }

            const int8_t slot = mapped_slot_[bt_slot];
            if (has_fresh_report) {
                const uint64_t previous_report_us = last_report_seen_us_[bt_slot];
                last_poll_gap_us_[bt_slot] = (previous_report_us > 0 && data.last_report_time_us > previous_report_us)
                                                 ? clampUsToU32(data.last_report_time_us - previous_report_us)
                                                 : 0;
                last_report_gap_us_[bt_slot] = data.last_report_gap_us;
                last_reports_since_poll_[bt_slot] =
                    (last_report_count_seen_[bt_slot] > 0) ? (data.report_count - last_report_count_seen_[bt_slot]) : 1;
                last_report_count_seen_[bt_slot] = data.report_count;
                last_report_seen_us_[bt_slot] = data.last_report_time_us;
                last_input_cache_[bt_slot] = fromGamepad(bt_slot, data, now_us);
                have_last_input_[bt_slot] = true;
            }

            if (slot >= 0 && slot < bluetooth::ControllerManager::kMaxSlots) {
                if (has_fresh_report && have_last_input_[bt_slot]) {
                    controller_manager_->recordInput(static_cast<uint8_t>(slot), now_ms, last_input_cache_[bt_slot]);
                    if (split_tembed_[bt_slot]) {
                        publishTEmbedSplit(bt_slot, last_input_cache_[bt_slot], now_ms);
                    } else {
                        const auto role = controller_manager_->getSlot(static_cast<uint8_t>(slot)).role;
                        publishByRole(role, last_input_cache_[bt_slot], now_ms);
                    }
                } else if (split_tembed_[bt_slot] && have_last_input_[bt_slot]) {
                    // T-Embed BLE reports can be sparse while the link is
                    // still connected. Stop any last command locally, but do
                    // not churn the BT connection just because no duplicate
                    // neutral report arrived.
                    const uint64_t stop_after_ms = tembedLocalStopMs(controller_manager_->getTimeoutMs());
                    const uint64_t quiet_ms = reportAgeMs(bt_slot, now_ms);
                    if (!isTEmbedIdleInput(last_input_cache_[bt_slot]) && quiet_ms > stop_after_ms) {
                        source_local_stop_count_[bt_slot]++;
                        const uint32_t report_age_us = clampUsToU32(quiet_ms * 1000ULL);
                        ESP_LOGD(TAG, "T-Embed local stop: bt_slot=%d age=%llu ms", bt_slot,
                                 static_cast<unsigned long long>(quiet_ms));
                        publishTEmbedZero(true,
                                          static_cast<uint16_t>(messages::ControllerInput::SOURCE_DIAG_TEMBED_SPLIT |
                                                                messages::ControllerInput::SOURCE_DIAG_LOCAL_STOP),
                                          report_age_us, last_report_gap_us_[bt_slot],
                                          source_local_stop_count_[bt_slot], now_ms);
                        last_input_cache_[bt_slot] = messages::ControllerInput{};
                        last_input_cache_[bt_slot].is_connected = true;
                        last_input_cache_[bt_slot].source_diagnostic_flags =
                            static_cast<uint16_t>(messages::ControllerInput::SOURCE_DIAG_TEMBED_SPLIT |
                                                  messages::ControllerInput::SOURCE_DIAG_LOCAL_STOP);
                        last_input_cache_[bt_slot].source_report_age_us = report_age_us;
                        last_input_cache_[bt_slot].source_report_gap_us = last_report_gap_us_[bt_slot];
                        last_input_cache_[bt_slot].source_local_stop_count = source_local_stop_count_[bt_slot];
                    }
                    if (quiet_ms > kTEmbedReportQuietDisconnectMs) {
                        ESP_LOGW(
                            TAG,
                            "T-Embed report quiet exceeded reconnect threshold: bt_slot=%d mapped_slot=%d age=%llu "
                            "ms threshold=%llu ms last_report_us=%llu",
                            bt_slot, static_cast<int>(slot), static_cast<unsigned long long>(quiet_ms),
                            static_cast<unsigned long long>(kTEmbedReportQuietDisconnectMs),
                            static_cast<unsigned long long>(last_report_seen_us_[bt_slot]));
                        publishTEmbedZero(false, 0, 0, 0, 0, now_ms);
                        controller_manager_->onDisconnect(static_cast<uint8_t>(slot), now_ms);
                        requestBluepadReconnect(bt_slot, now_ms, true, "tembed_report_quiet");
                        continue;
                    }
                    controller_manager_->feedSlotWatchdog(static_cast<uint8_t>(slot), now_ms);
                } else if (have_last_input_[bt_slot] && isIdleInput(last_input_cache_[bt_slot])) {
                    // Joy-Cons may stop sending duplicate neutral reports when
                    // untouched. Bluepad still reports the link as connected,
                    // so keep the connection watchdog alive only for a known
                    // neutral state. A stale non-neutral command still times
                    // out into the disconnect safety path.
                    controller_manager_->feedSlotWatchdog(static_cast<uint8_t>(slot), now_ms);
                } else if (now_ms - connect_time_ms_[bt_slot] < kConnectGraceMs) {
                    // Time-based grace period after BT connect.  JoyCons can
                    // send one early report then go quiet for >500 ms while
                    // finishing pairing.  Feed the watchdog so it doesn't trip
                    // before regular reports begin.
                    controller_manager_->feedSlotWatchdog(static_cast<uint8_t>(slot), now_ms);
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

        reconcileDroppedManagerSlots(now_ms);
        serviceTEmbedDiscoveryRetry(now_ms);
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

    static void formatMac(const uint8_t btaddr[6], char out[18]) {
        std::snprintf(out, 18, "%02X:%02X:%02X:%02X:%02X:%02X", btaddr[0], btaddr[1], btaddr[2], btaddr[3], btaddr[4],
                      btaddr[5]);
    }

    static bluetooth::MacAddress macFromData(const chopper_gamepad_data_t& data) {
        bluetooth::MacAddress mac{};
        std::memcpy(mac.addr, data.btaddr, sizeof(mac.addr));
        return mac;
    }

    void resetObservedSlot(int bt_slot) {
        observed_connected_[bt_slot] = false;
        mapped_slot_[bt_slot] = -1;
        last_report_seen_us_[bt_slot] = 0;
        last_report_gap_us_[bt_slot] = 0;
        last_poll_gap_us_[bt_slot] = 0;
        last_report_count_seen_[bt_slot] = 0;
        last_reports_since_poll_[bt_slot] = 0;
        connect_time_ms_[bt_slot] = 0;
        have_last_input_[bt_slot] = false;
        split_tembed_[bt_slot] = false;
        last_tembed_buttons_[bt_slot] = 0;
        last_tembed_misc_[bt_slot] = 0;
        last_tembed_dpad_[bt_slot] = 0;
    }

    bool mappedSlotStillActive(int bt_slot, const chopper_gamepad_data_t& data) const {
        if (bt_slot < 0 || bt_slot >= CHOPPER_BT_MAX_DEVICES) {
            return false;
        }
        const int8_t slot_index = mapped_slot_[bt_slot];
        if (slot_index < 0 || slot_index >= bluetooth::ControllerManager::kMaxSlots) {
            return false;
        }

        const auto& slot = controller_manager_->getSlot(static_cast<uint8_t>(slot_index));
        return slot.isActive() && slot.mac == macFromData(data);
    }

    uint64_t reportAgeMs(int bt_slot, uint64_t now_ms) const {
        const uint64_t last_report_ms = last_report_seen_us_[bt_slot] / 1000ULL;
        if (last_report_ms == 0 || now_ms < last_report_ms) {
            return 0;
        }
        return now_ms - last_report_ms;
    }

    void reconcileDroppedManagerSlots(uint64_t now_ms) {
        for (int bt_slot = 0; bt_slot < CHOPPER_BT_MAX_DEVICES; ++bt_slot) {
            if (!observed_connected_[bt_slot]) {
                continue;
            }
            const int8_t slot_index = mapped_slot_[bt_slot];
            if (slot_index < 0 || slot_index >= bluetooth::ControllerManager::kMaxSlots) {
                continue;
            }

            const auto& slot = controller_manager_->getSlot(static_cast<uint8_t>(slot_index));
            if (slot.isActive()) {
                continue;
            }

            const auto role = slot.role;
            if (split_tembed_[bt_slot]) {
                publishTEmbedZero(false, 0, 0, 0, 0, now_ms);
            } else {
                publishZeroForDisconnectedRole(role);
            }
            ESP_LOGW(TAG,
                     "Controller manager dropped bt_slot=%d mapped_slot=%d split_tembed=%d report_age=%llu ms; "
                     "forcing Bluepad reconnect",
                     bt_slot, static_cast<int>(slot_index), split_tembed_[bt_slot] ? 1 : 0,
                     static_cast<unsigned long long>(reportAgeMs(bt_slot, now_ms)));
            requestBluepadReconnect(bt_slot, now_ms, split_tembed_[bt_slot], "manager_slot_dropped");
        }
    }

    void enableTEmbedDiscoveryRetry(uint64_t now_ms) {
        tembed_discovery_retry_enabled_ = true;
        next_tembed_discovery_retry_ms_ = now_ms;
        ESP_LOGW(TAG, "Persistent T-Embed LE discovery retry enabled: now=%llu interval=%llu ms",
                 static_cast<unsigned long long>(now_ms), static_cast<unsigned long long>(kTEmbedDiscoveryRetryMs));
    }

    void disableTEmbedDiscoveryRetry() {
        if (tembed_discovery_retry_enabled_) {
            ESP_LOGD(TAG, "Persistent T-Embed LE discovery retry disabled");
        }
        tembed_discovery_retry_enabled_ = false;
        next_tembed_discovery_retry_ms_ = 0;
    }

    void serviceTEmbedDiscoveryRetry(uint64_t now_ms) {
        if (!tembed_discovery_retry_enabled_) {
            return;
        }
        if (now_ms < next_tembed_discovery_retry_ms_) {
            return;
        }

        ESP_LOGD(TAG, "Persistent T-Embed LE discovery retry tick: now=%llu", static_cast<unsigned long long>(now_ms));
        chopper_bt_restart_le_discovery();
        next_tembed_discovery_retry_ms_ = now_ms + kTEmbedDiscoveryRetryMs;
    }

    void requestBluepadReconnect(int bt_slot, uint64_t now_ms, bool retry_tembed_discovery, const char* reason) {
        const uint64_t last_report_us = last_report_seen_us_[bt_slot];
        ESP_LOGW(TAG,
                 "Requesting Bluepad reconnect: reason=%s bt_slot=%d mapped_slot=%d retry_tembed=%d split_tembed=%d "
                 "report_age=%llu ms last_report_us=%llu",
                 reason ? reason : "(none)", bt_slot, static_cast<int>(mapped_slot_[bt_slot]),
                 retry_tembed_discovery ? 1 : 0, split_tembed_[bt_slot] ? 1 : 0,
                 static_cast<unsigned long long>(reportAgeMs(bt_slot, now_ms)),
                 static_cast<unsigned long long>(last_report_us));
        if (chopper_bt_disconnect_gamepad_with_reason(bt_slot, reason) != 0) {
            ESP_LOGW(TAG, "Unable to request Bluepad disconnect for bt_slot=%d reason=%s", bt_slot,
                     reason ? reason : "(none)");
        }
        if (retry_tembed_discovery) {
            chopper_bt_restart_le_discovery();
            enableTEmbedDiscoveryRetry(now_ms);
        } else {
            chopper_bt_restart_discovery();
        }

        resetObservedSlot(bt_slot);
        last_report_seen_us_[bt_slot] = last_report_us;
    }

    void handleConnectionTransitions(int bt_slot, const chopper_gamepad_data_t& data, bool has_fresh_report,
                                     uint64_t now_ms) {
        if (data.connected && !observed_connected_[bt_slot]) {
            bluetooth::MacAddress mac = macFromData(data);
            char mac_str[18];
            formatMac(data.btaddr, mac_str);
            const bool is_tembed = input::isTEmbedMac(mac_str);
            if (is_tembed && !has_fresh_report) {
                if (connect_time_ms_[bt_slot] == 0) {
                    connect_time_ms_[bt_slot] = now_ms;
                } else if (now_ms - connect_time_ms_[bt_slot] >= kTEmbedFirstReportTimeoutMs) {
                    ESP_LOGW(
                        TAG,
                        "T-Embed connected without first report; forcing Bluepad reconnect: bt_slot=%d elapsed=%llu "
                        "ms threshold=%llu ms",
                        bt_slot, static_cast<unsigned long long>(now_ms - connect_time_ms_[bt_slot]),
                        static_cast<unsigned long long>(kTEmbedFirstReportTimeoutMs));
                    requestBluepadReconnect(bt_slot, now_ms, true, "tembed_first_report_timeout");
                }
                return;
            }

            mapped_slot_[bt_slot] = controller_manager_->onConnect(mac, data.controller_type, 0, 0, now_ms);
            observed_connected_[bt_slot] = true;
            connect_time_ms_[bt_slot] = now_ms;
            split_tembed_[bt_slot] = is_tembed;
            if (split_tembed_[bt_slot]) {
                disableTEmbedDiscoveryRetry();
                ESP_LOGI(TAG, "T-Embed split path active: bt_slot=%d mac=%s", bt_slot, mac_str);
            }

            // Select per-controller-type intent maps for the assigned role.
            if (mapped_slot_[bt_slot] >= 0) {
                const auto role = controller_manager_->getSlot(static_cast<uint8_t>(mapped_slot_[bt_slot])).role;
                if (role == bluetooth::ControllerRole::DRIVE) {
                    drive_intent_map_ = input::driveIntentMapForController(data.controller_type);
                } else if (role == bluetooth::ControllerRole::DOME) {
                    dome_intent_map_ = input::domeIntentMapForController(data.controller_type);
                }
            }
        } else if (!data.connected && observed_connected_[bt_slot]) {
            bluetooth::ControllerRole role = bluetooth::ControllerRole::UNASSIGNED;
            const bool was_tembed = split_tembed_[bt_slot];
            ESP_LOGW(TAG, "BT controller disconnected: bt_slot=%d mapped_slot=%d was_tembed=%d", bt_slot,
                     static_cast<int>(mapped_slot_[bt_slot]), was_tembed ? 1 : 0);
            if (mapped_slot_[bt_slot] >= 0) {
                const auto slot = static_cast<uint8_t>(mapped_slot_[bt_slot]);
                role = controller_manager_->getSlot(slot).role;
                controller_manager_->onDisconnect(slot, now_ms);
            }
            if (was_tembed) {
                publishTEmbedZero(false, 0, 0, 0, 0, now_ms);
                enableTEmbedDiscoveryRetry(now_ms);
            } else {
                publishZeroForDisconnectedRole(role);
            }
            resetObservedSlot(bt_slot);
            if (was_tembed) {
                chopper_bt_restart_le_discovery();
            } else {
                chopper_bt_restart_discovery();
            }
        }
    }

    static uint32_t clampUsToU32(uint64_t value_us) {
        static constexpr uint64_t kMaxU32 = 0xffffffffULL;
        return value_us > kMaxU32 ? static_cast<uint32_t>(kMaxU32) : static_cast<uint32_t>(value_us);
    }

    messages::ControllerInput fromGamepad(int bt_slot, const chopper_gamepad_data_t& data, uint64_t now_us) {
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
        const auto calibration = calibrationForSlot(bt_slot);
        const auto* profile = profileForSlot(bt_slot);
        out.axis_x_normalized =
            normalizeCalibratedAxisWithProfile(out.axis_x, calibration.offset_x, calibration.invert_x, profile);
        out.axis_y_normalized =
            normalizeCalibratedAxisWithProfile(out.axis_y, calibration.offset_y, calibration.invert_y, profile);
        out.axis_rx_normalized = normalizeAxisWithProfile(out.axis_rx, profile);
        out.axis_ry_normalized = normalizeAxisWithProfile(out.axis_ry, profile);
        // Runtime currently has no separate slew-filter node; feed DriveNode
        // with normalized axes so input maps to motor commands as expected.
        out.axis_x_slew = out.axis_x_normalized;
        out.axis_y_slew = out.axis_y_normalized;
        out.source_diagnostic_flags = messages::ControllerInput::SOURCE_DIAG_FRESH_REPORT;
        if (split_tembed_[bt_slot]) {
            out.source_diagnostic_flags = static_cast<uint16_t>(out.source_diagnostic_flags |
                                                                messages::ControllerInput::SOURCE_DIAG_TEMBED_SPLIT);
        }
        out.source_report_gap_us = last_report_gap_us_[bt_slot];
        out.source_report_age_us = (data.last_report_time_us > 0 && now_us >= data.last_report_time_us)
                                       ? clampUsToU32(now_us - data.last_report_time_us)
                                       : 0;
        out.source_local_stop_count = source_local_stop_count_[bt_slot];

        if (trace_input_ && (last_buttons_[bt_slot] != out.buttons || last_misc_[bt_slot] != out.misc_buttons ||
                             last_dpad_[bt_slot] != out.dpad)) {
            ESP_LOGI(TAG, "slot=%d mac=%s dpad=0x%02x buttons=0x%04x misc=0x%02x axes=(%ld,%ld,%ld,%ld)", bt_slot,
                     out.mac_address, out.dpad, static_cast<unsigned>(out.buttons),
                     static_cast<unsigned>(out.misc_buttons), static_cast<long>(out.axis_x),
                     static_cast<long>(out.axis_y), static_cast<long>(out.axis_rx), static_cast<long>(out.axis_ry));
            last_buttons_[bt_slot] = out.buttons;
            last_misc_[bt_slot] = out.misc_buttons;
            last_dpad_[bt_slot] = out.dpad;
        }

        return out;
    }

    static bool isIdleInput(const messages::ControllerInput& input) {
        constexpr float kIdleAxisThreshold = 0.05f;
        const bool axes_idle = std::fabs(input.axis_x_normalized) <= kIdleAxisThreshold &&
                               std::fabs(input.axis_y_normalized) <= kIdleAxisThreshold &&
                               std::fabs(input.axis_rx_normalized) <= kIdleAxisThreshold &&
                               std::fabs(input.axis_ry_normalized) <= kIdleAxisThreshold;
        return axes_idle && input.dpad == 0 && input.buttons == 0 && input.misc_buttons == 0 && input.brake == 0 &&
               input.throttle == 0;
    }

    static bool isTEmbedIdleInput(const messages::ControllerInput& input) {
        constexpr float kIdleAxisThreshold = 0.05f;
        const bool axes_idle = std::fabs(input.axis_x_normalized) <= kIdleAxisThreshold &&
                               std::fabs(input.axis_y_normalized) <= kIdleAxisThreshold &&
                               std::fabs(input.axis_rx_normalized) <= kIdleAxisThreshold;
        const bool mapped_buttons_idle = !input.button_a && !input.button_b && !input.button_x && !input.button_y &&
                                         !input.button_l1 && !input.button_l2 && !input.button_r1 && !input.button_r2 &&
                                         !input.button_thumb_l && !input.button_thumb_r && !input.misc_select &&
                                         !input.misc_start;
        return axes_idle && mapped_buttons_idle;
    }

    static bool hasTEmbedAxisActivity(const messages::ControllerInput& input) {
        return std::fabs(input.axis_x_normalized) > kTEmbedIdleAxisThreshold ||
               std::fabs(input.axis_y_normalized) > kTEmbedIdleAxisThreshold ||
               std::fabs(input.axis_rx_normalized) > kTEmbedIdleAxisThreshold;
    }

    static bool hasTEmbedButtonActivity(const messages::ControllerInput& input) {
        return input.dpad != 0 || input.buttons != 0 || input.misc_buttons != 0;
    }

    static bool hasDriveOutputActivity(const messages::ControllerInput& input) {
        return std::fabs(input.axis_x_normalized) > kRoleActivityAxisThreshold ||
               std::fabs(input.axis_y_normalized) > kRoleActivityAxisThreshold || input.intent_periscope_up ||
               input.intent_periscope_down || input.intent_periscope_spin_left || input.intent_periscope_spin_right ||
               input.intent_dome_doors_toggle || input.intent_body_left_door_toggle ||
               input.intent_body_right_door_toggle || input.intent_body_utility_toggle ||
               input.intent_carpet_mode_toggle || input.intent_carpet_mode_active || input.intent_dome_rotate_left ||
               input.intent_volume_down || input.intent_volume_up;
    }

    static bool hasDomeOutputActivity(const messages::ControllerInput& input) {
        return std::fabs(input.axis_rx_normalized) > kRoleActivityAxisThreshold ||
               std::fabs(input.axis_ry_normalized) > kRoleActivityAxisThreshold || input.intent_neck_toggle ||
               input.intent_neck_height_up || input.intent_neck_height_down || input.intent_sound_a ||
               input.intent_sound_b || input.intent_sound_random || input.intent_dome_rotate_right ||
               input.intent_eye_color_toggle || input.intent_dome_random_toggle || input.intent_face_tracking_toggle;
    }

    static const char* tembedActivityKind(bool axis_active, bool button_active) {
        if (axis_active && button_active) {
            return "axis+button";
        }
        if (axis_active) {
            return "axis";
        }
        if (button_active) {
            return "button";
        }
        return "calibrated";
    }

    static int32_t axisMilli(float value) { return static_cast<int32_t>(value * 1000.0f); }

    static uint64_t tembedLocalStopMs(uint64_t manager_timeout_ms) {
        return manager_timeout_ms > kTEmbedLocalStopMs ? manager_timeout_ms : kTEmbedLocalStopMs;
    }

    void publishByRole(bluetooth::ControllerRole role, const messages::ControllerInput& input, uint64_t now_ms = 0) {
        messages::ControllerInput mapped = input;
        if (role == bluetooth::ControllerRole::DRIVE) {
            input::setDriveIntentsFromRaw(mapped, drive_intent_map_);
            if (now_ms > 0 && hasDriveOutputActivity(mapped)) {
                last_drive_role_active_ms_ = now_ms;
            }
        } else if (role == bluetooth::ControllerRole::DOME) {
            input::setDomeIntentsFromRaw(mapped, dome_intent_map_);
            detectFaceTrackingHold(mapped);
            if (now_ms > 0 && hasDomeOutputActivity(mapped)) {
                last_dome_role_active_ms_ = now_ms;
            }
        }
        switch (role) {
            case bluetooth::ControllerRole::DRIVE:
                if (drive_pub_)
                    drive_pub_->publish(mapped);
                break;
            case bluetooth::ControllerRole::DOME:
                if (dome_pub_)
                    dome_pub_->publish(mapped);
                break;
            case bluetooth::ControllerRole::ANIMATION:
                if (animation_pub_)
                    animation_pub_->publish(mapped);
                break;
            case bluetooth::ControllerRole::CAMERA:
                if (camera_pub_)
                    camera_pub_->publish(mapped);
                break;
            case bluetooth::ControllerRole::UNASSIGNED:
            case bluetooth::ControllerRole::VAMBRACE:
                break;
        }
    }

    bool normalRoleActiveRecently(bluetooth::ControllerRole role, uint64_t now_ms) const {
        uint64_t last_active_ms = 0;
        if (role == bluetooth::ControllerRole::DRIVE) {
            last_active_ms = last_drive_role_active_ms_;
        } else if (role == bluetooth::ControllerRole::DOME) {
            last_active_ms = last_dome_role_active_ms_;
        }
        return last_active_ms > 0 && now_ms >= last_active_ms && (now_ms - last_active_ms) <= kVambraceConflictHoldMs;
    }

    void publishTEmbedSplit(int bt_slot, const messages::ControllerInput& input, uint64_t now_ms) {
        if (!isTEmbedIdleInput(input) && input.source_report_gap_us > kTEmbedReportGapWarnUs &&
            now_ms >= next_tembed_gap_log_ms_[bt_slot]) {
            const bool axis_active = hasTEmbedAxisActivity(input);
            const bool button_active = hasTEmbedButtonActivity(input);
            const char* kind = tembedActivityKind(axis_active, button_active);
            const uint64_t warn_gap_us = tembedLocalStopMs(controller_manager_->getTimeoutMs()) * 1000ULL;
            if (axis_active && input.source_report_gap_us >= warn_gap_us) {
                ESP_LOGW(TAG,
                         "T-Embed input report gap: kind=%s bt_slot=%d cb_gap=%lu us poll_gap=%lu us age=%lu us "
                         "reports=%lu raw_axes=(%ld,%ld,%ld,%ld) norm_milli=(%ld,%ld,%ld) buttons=0x%04x "
                         "misc=0x%02x dpad=0x%02x local_stops=%lu",
                         kind, bt_slot, static_cast<unsigned long>(input.source_report_gap_us),
                         static_cast<unsigned long>(last_poll_gap_us_[bt_slot]),
                         static_cast<unsigned long>(input.source_report_age_us),
                         static_cast<unsigned long>(last_reports_since_poll_[bt_slot]), static_cast<long>(input.axis_x),
                         static_cast<long>(input.axis_y), static_cast<long>(input.axis_rx),
                         static_cast<long>(input.axis_ry), static_cast<long>(axisMilli(input.axis_x_normalized)),
                         static_cast<long>(axisMilli(input.axis_y_normalized)),
                         static_cast<long>(axisMilli(input.axis_rx_normalized)), static_cast<unsigned>(input.buttons),
                         static_cast<unsigned>(input.misc_buttons), static_cast<unsigned>(input.dpad),
                         static_cast<unsigned long>(input.source_local_stop_count));
            } else {
                ESP_LOGD(TAG,
                         "T-Embed input report gap: kind=%s bt_slot=%d cb_gap=%lu us poll_gap=%lu us age=%lu us "
                         "reports=%lu raw_axes=(%ld,%ld,%ld,%ld) norm_milli=(%ld,%ld,%ld) buttons=0x%04x "
                         "misc=0x%02x dpad=0x%02x local_stops=%lu",
                         kind, bt_slot, static_cast<unsigned long>(input.source_report_gap_us),
                         static_cast<unsigned long>(last_poll_gap_us_[bt_slot]),
                         static_cast<unsigned long>(input.source_report_age_us),
                         static_cast<unsigned long>(last_reports_since_poll_[bt_slot]), static_cast<long>(input.axis_x),
                         static_cast<long>(input.axis_y), static_cast<long>(input.axis_rx),
                         static_cast<long>(input.axis_ry), static_cast<long>(axisMilli(input.axis_x_normalized)),
                         static_cast<long>(axisMilli(input.axis_y_normalized)),
                         static_cast<long>(axisMilli(input.axis_rx_normalized)), static_cast<unsigned>(input.buttons),
                         static_cast<unsigned>(input.misc_buttons), static_cast<unsigned>(input.dpad),
                         static_cast<unsigned long>(input.source_local_stop_count));
            }
            next_tembed_gap_log_ms_[bt_slot] = now_ms + kTEmbedGapLogMinIntervalMs;
        }
        const bool drive_blocked = normalRoleActiveRecently(bluetooth::ControllerRole::DRIVE, now_ms);
        const bool dome_blocked = normalRoleActiveRecently(bluetooth::ControllerRole::DOME, now_ms);
        const auto drive_input = input::makeTEmbedDriveInput(input);
        const auto dome_input = input::makeTEmbedDomeInput(input);
        const bool button_changed = input.buttons != last_tembed_buttons_[bt_slot] ||
                                    input.misc_buttons != last_tembed_misc_[bt_slot] ||
                                    input.dpad != last_tembed_dpad_[bt_slot];
        if (button_changed) {
            ESP_LOGD(TAG,
                     "T-Embed button split: bt_slot=%d raw_buttons=0x%04x raw_misc=0x%02x "
                     "raw_dpad=0x%02x blocked[drive=%d dome=%d] "
                     "drive[peri_up=%d peri_down=%d spin_l=%d spin_r=%d doors=%d util=%d] "
                     "dome[sound_a=%d sound_b=%d random=%d eyes=%d tracking=%d]",
                     bt_slot, static_cast<unsigned>(input.buttons), static_cast<unsigned>(input.misc_buttons),
                     static_cast<unsigned>(input.dpad), drive_blocked ? 1 : 0, dome_blocked ? 1 : 0,
                     drive_input.intent_periscope_up ? 1 : 0, drive_input.intent_periscope_down ? 1 : 0,
                     drive_input.intent_periscope_spin_left ? 1 : 0, drive_input.intent_periscope_spin_right ? 1 : 0,
                     drive_input.intent_dome_doors_toggle ? 1 : 0, drive_input.intent_body_utility_toggle ? 1 : 0,
                     dome_input.intent_sound_a ? 1 : 0, dome_input.intent_sound_b ? 1 : 0,
                     dome_input.intent_sound_random ? 1 : 0, dome_input.intent_eye_color_toggle ? 1 : 0,
                     dome_input.intent_face_tracking_toggle ? 1 : 0);
            last_tembed_buttons_[bt_slot] = input.buttons;
            last_tembed_misc_[bt_slot] = input.misc_buttons;
            last_tembed_dpad_[bt_slot] = input.dpad;
        }
        if (drive_pub_ && !drive_blocked) {
            drive_pub_->publish(drive_input);
        }
        if (dome_pub_ && !dome_blocked) {
            dome_pub_->publish(dome_input);
        }
    }

    void publishTEmbedZero(bool connected = false, uint16_t source_flags = 0, uint32_t source_age_us = 0,
                           uint32_t source_gap_us = 0, uint32_t local_stop_count = 0, uint64_t now_ms = 0) {
        messages::ControllerInput zero{};
        zero.has_intents = true;
        zero.is_connected = connected;
        zero.source_diagnostic_flags = source_flags;
        zero.source_report_age_us = source_age_us;
        zero.source_report_gap_us = source_gap_us;
        zero.source_local_stop_count = local_stop_count;
        const bool drive_blocked = now_ms > 0 && normalRoleActiveRecently(bluetooth::ControllerRole::DRIVE, now_ms);
        const bool dome_blocked = now_ms > 0 && normalRoleActiveRecently(bluetooth::ControllerRole::DOME, now_ms);
        if (drive_pub_ && !drive_blocked) {
            drive_pub_->publish(zero);
        }
        if (dome_pub_ && !dome_blocked) {
            dome_pub_->publish(zero);
        }
    }

    void publishZeroForDisconnectedRole(bluetooth::ControllerRole role) {
        if (role != bluetooth::ControllerRole::DRIVE && role != bluetooth::ControllerRole::DOME) {
            return;
        }
        messages::ControllerInput zero{};
        zero.has_intents = true;
        zero.is_connected = false;
        publishByRole(role, zero);
    }

    input::AxisCalibration calibrationForSlot(int bt_slot) const {
        if (bt_slot < 0 || bt_slot >= CHOPPER_BT_MAX_DEVICES) {
            return {};
        }
        const int8_t slot = mapped_slot_[bt_slot];
        if (slot < 0 || slot >= bluetooth::ControllerManager::kMaxSlots) {
            return {};
        }
        const auto role = controller_manager_->getSlot(static_cast<uint8_t>(slot)).role;
        if (role == bluetooth::ControllerRole::DRIVE || role == bluetooth::ControllerRole::VAMBRACE) {
            return drive_axis_cal_;
        }
        if (role == bluetooth::ControllerRole::DOME) {
            return dome_axis_cal_;
        }
        return {};
    }

    const bluetooth::ButtonMappingProfile* profileForSlot(int bt_slot) const {
        if (bt_slot < 0 || bt_slot >= CHOPPER_BT_MAX_DEVICES) {
            return nullptr;
        }
        const int8_t slot = mapped_slot_[bt_slot];
        if (slot < 0 || slot >= bluetooth::ControllerManager::kMaxSlots) {
            return nullptr;
        }
        return controller_manager_->getProfileForSlot(static_cast<uint8_t>(slot));
    }

    static float normalizeAxisWithProfile(int32_t raw, const bluetooth::ButtonMappingProfile* profile) {
        if (profile != nullptr) {
            return profile->normalizeAxis(raw);
        }
        return input::normalizeControllerAxis(raw);
    }

    static float normalizeCalibratedAxisWithProfile(int32_t raw, int32_t offset, bool inverted,
                                                    const bluetooth::ButtonMappingProfile* profile) {
        return normalizeAxisWithProfile(input::applyAxisCalibrationRaw(raw, offset, inverted), profile);
    }

    bool refreshControllerCalibration() {
        auto& ps = core::ParameterServer::getInstance();
        bool ok = true;
        ok &= ps.get("ctrl.drive.offset_x", drive_axis_cal_.offset_x);
        ok &= ps.get("ctrl.drive.offset_y", drive_axis_cal_.offset_y);
        ok &= ps.get("ctrl.drive.invert_x", drive_axis_cal_.invert_x);
        ok &= ps.get("ctrl.drive.invert_y", drive_axis_cal_.invert_y);
        ok &= ps.get("ctrl.dome.offset_x", dome_axis_cal_.offset_x);
        ok &= ps.get("ctrl.dome.offset_y", dome_axis_cal_.offset_y);
        ok &= ps.get("ctrl.dome.invert_x", dome_axis_cal_.invert_x);
        ok &= ps.get("ctrl.dome.invert_y", dome_axis_cal_.invert_y);
        if (!ok) {
            ESP_LOGE(TAG, "Missing controller calibration parameters");
        }
        return ok;
    }

    /// Detect SL+SR (L1+R1) held for 2 seconds on the dome controller.
    /// Sets intent_face_tracking_toggle = true for one publish cycle
    /// when the hold threshold is reached, then suppresses until released.
    void detectFaceTrackingHold(messages::ControllerInput& input) {
        const bool both_held = input.button_l1 && input.button_r1;
        const auto now_ms = static_cast<uint64_t>(esp_timer_get_time() / 1000ULL);

        if (both_held) {
            if (tracking_hold_start_ms_ == 0) {
                tracking_hold_start_ms_ = now_ms;
            }
            if (!tracking_hold_fired_ && (now_ms - tracking_hold_start_ms_) >= kTrackingHoldMs) {
                input.intent_face_tracking_toggle = true;
                tracking_hold_fired_ = true;
            }
        } else {
            tracking_hold_start_ms_ = 0;
            tracking_hold_fired_ = false;
        }
    }

    static constexpr uint64_t kConnectGraceMs = 2000;  ///< Post-connect watchdog grace period
    static constexpr uint64_t kTEmbedFirstReportTimeoutMs = 1500;
    // The BLE/HOGP T-Embed path can deliver active reports at 180-360 ms
    // intervals under encoder load even when the link is healthy. Keep Joy-Con
    // watchdog behavior at 200 ms, but avoid injecting false neutral reports
    // between active T-Embed encoder updates.
    static constexpr uint64_t kTEmbedLocalStopMs = 400;
    static constexpr uint64_t kTEmbedReportQuietDisconnectMs = 30000;
    static constexpr uint64_t kTEmbedDiscoveryRetryMs = 5000;
    static constexpr uint32_t kTEmbedReportGapWarnUs = 100000;
    static constexpr uint64_t kTEmbedGapLogMinIntervalMs = 1000;
    static constexpr float kTEmbedIdleAxisThreshold = 0.05f;
    static constexpr float kRoleActivityAxisThreshold = 0.05f;
    static constexpr uint64_t kVambraceConflictHoldMs = 250;
    static constexpr uint64_t kTrackingHoldMs = 2000;

    bluetooth::ControllerManager* controller_manager_;
    bool trace_input_;
    double hz_;

    core::TypedPublisherPtr<messages::ControllerInput> drive_pub_;
    core::TypedPublisherPtr<messages::ControllerInput> dome_pub_;
    core::TypedPublisherPtr<messages::ControllerInput> animation_pub_;
    core::TypedPublisherPtr<messages::ControllerInput> camera_pub_;
    input::DriveIntentMap drive_intent_map_{input::defaultDriveIntentMap()};
    input::DomeIntentMap dome_intent_map_{input::defaultDomeIntentMap()};
    input::AxisCalibration drive_axis_cal_{};
    input::AxisCalibration dome_axis_cal_{};

    bool observed_connected_[CHOPPER_BT_MAX_DEVICES];
    int8_t mapped_slot_[CHOPPER_BT_MAX_DEVICES];
    uint16_t last_buttons_[CHOPPER_BT_MAX_DEVICES];
    uint8_t last_misc_[CHOPPER_BT_MAX_DEVICES];
    uint8_t last_dpad_[CHOPPER_BT_MAX_DEVICES];
    uint64_t last_report_seen_us_[CHOPPER_BT_MAX_DEVICES];
    uint32_t last_report_gap_us_[CHOPPER_BT_MAX_DEVICES];
    uint32_t last_poll_gap_us_[CHOPPER_BT_MAX_DEVICES];
    uint32_t last_report_count_seen_[CHOPPER_BT_MAX_DEVICES];
    uint32_t last_reports_since_poll_[CHOPPER_BT_MAX_DEVICES];
    uint64_t connect_time_ms_[CHOPPER_BT_MAX_DEVICES];
    messages::ControllerInput last_input_cache_[CHOPPER_BT_MAX_DEVICES];
    bool have_last_input_[CHOPPER_BT_MAX_DEVICES];
    bool split_tembed_[CHOPPER_BT_MAX_DEVICES];
    uint32_t source_local_stop_count_[CHOPPER_BT_MAX_DEVICES];
    uint64_t next_tembed_gap_log_ms_[CHOPPER_BT_MAX_DEVICES];
    uint16_t last_tembed_buttons_[CHOPPER_BT_MAX_DEVICES];
    uint8_t last_tembed_misc_[CHOPPER_BT_MAX_DEVICES];
    uint8_t last_tembed_dpad_[CHOPPER_BT_MAX_DEVICES];
    bool tembed_discovery_retry_enabled_;
    uint64_t next_tembed_discovery_retry_ms_;
    uint64_t last_drive_role_active_ms_ = 0;
    uint64_t last_dome_role_active_ms_ = 0;

    // Face tracking SL+SR hold state
    uint64_t tracking_hold_start_ms_ = 0;
    bool tracking_hold_fired_ = false;
};

}  // namespace nodes
}  // namespace chopper

#endif  // ESP_PLATFORM
