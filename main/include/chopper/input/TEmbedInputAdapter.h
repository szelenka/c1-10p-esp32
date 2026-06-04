#pragma once

#include "chopper/config/HardwareConfig.h"
#include "chopper/messages/CommonMessages.h"

#include <cstring>

namespace chopper::input {

inline constexpr const char* kTEmbedMac = config::bluetooth::TEMBED_MAC;

inline bool isTEmbedMac(const char* mac_address) {
    return mac_address != nullptr && std::strncmp(mac_address, kTEmbedMac, 17) == 0;
}

inline messages::ControllerInput makeTEmbedDriveInput(const messages::ControllerInput& raw) {
    messages::ControllerInput out{};
    out.controller_id = raw.controller_id;
    out.battery_level = raw.battery_level;
    out.is_connected = raw.is_connected;
    out.has_data = raw.has_data;
    out.is_gamepad = raw.is_gamepad;
    out.is_mouse = raw.is_mouse;
    out.is_balance_board = raw.is_balance_board;
    out.is_keyboard = raw.is_keyboard;
    std::memcpy(out.mac_address, raw.mac_address, sizeof(out.mac_address));

    out.axis_x = raw.axis_x;
    out.axis_y = raw.axis_y;
    out.axis_x_normalized = raw.axis_x_normalized;
    out.axis_y_normalized = raw.axis_y_normalized;
    out.axis_x_slew = raw.axis_x_slew;
    out.axis_y_slew = raw.axis_y_slew;
    out.source_diagnostic_flags = raw.source_diagnostic_flags;
    out.source_report_gap_us = raw.source_report_gap_us;
    out.source_report_age_us = raw.source_report_age_us;
    out.source_local_stop_count = raw.source_local_stop_count;

    out.has_intents = true;
    out.intent_periscope_up = raw.button_x;
    out.intent_periscope_down = raw.button_x;
    out.intent_periscope_spin_left = raw.button_thumb_l;
    out.intent_periscope_spin_right = raw.button_thumb_r;
    out.intent_dome_doors_toggle = raw.misc_select;
    out.intent_body_left_door_toggle = raw.button_l1;
    out.intent_body_right_door_toggle = raw.button_r1;
    out.intent_body_utility_toggle = raw.button_y;
    return out;
}

inline messages::ControllerInput makeTEmbedDomeInput(const messages::ControllerInput& raw) {
    messages::ControllerInput out{};
    out.controller_id = raw.controller_id;
    out.battery_level = raw.battery_level;
    out.is_connected = raw.is_connected;
    out.has_data = raw.has_data;
    out.is_gamepad = raw.is_gamepad;
    out.is_mouse = raw.is_mouse;
    out.is_balance_board = raw.is_balance_board;
    out.is_keyboard = raw.is_keyboard;
    std::memcpy(out.mac_address, raw.mac_address, sizeof(out.mac_address));

    out.axis_rx = raw.axis_rx;
    out.axis_rx_normalized = raw.axis_rx_normalized;
    out.source_diagnostic_flags = raw.source_diagnostic_flags;
    out.source_report_gap_us = raw.source_report_gap_us;
    out.source_report_age_us = raw.source_report_age_us;
    out.source_local_stop_count = raw.source_local_stop_count;

    out.has_intents = true;
    // Vambrace labels are wired to the Chopper tracks here:
    // BUTTON_B -> Carol of the Bells, BUTTON_A -> Mandalorian.
    out.intent_sound_a = raw.button_b;
    out.intent_sound_b = raw.button_a;
    out.intent_sound_random = raw.misc_start;
    // Vambrace sends EYES as R2 plus an L2 alias; either reports the same dome intent.
    out.intent_eye_color_toggle = raw.button_r2 || raw.button_l2;
    return out;
}

}  // namespace chopper::input
