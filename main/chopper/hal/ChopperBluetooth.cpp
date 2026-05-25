// SPDX-License-Identifier: Apache-2.0
// Native Bluepad32 platform for Chopper — replaces Arduino platform.

#include "sdkconfig.h"

#ifdef ESP_PLATFORM

#include "chopper/config/HardwareConfig.h"
#include "chopper/hal/ChopperBluetooth.h"

#include <stdio.h>
#include <string.h>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <btstack.h>
#include <esp_log.h>
#include <esp_timer.h>

#include <uni.h>
#include <bt/uni_bt_le.h>

// Sanity check
#ifndef CONFIG_BLUEPAD32_PLATFORM_CUSTOM
#error "Must use BLUEPAD32_PLATFORM_CUSTOM"
#endif

// --------------------------------------------------------------------------
// Internal state
// --------------------------------------------------------------------------

typedef struct {
    int8_t slot_idx;  // Index into controllers_[], -1 = unassigned
    uint8_t btaddr[6];
    uint16_t controller_type;
} chopper_instance_t;

_Static_assert(sizeof(chopper_instance_t) < HID_DEVICE_MAX_PLATFORM_DATA,
               "chopper_instance_t too big for platform_data");

static SemaphoreHandle_t controller_mutex_ = NULL;
static const char* TAG = "ChopperBT";
static chopper_gamepad_data_t controllers_[CHOPPER_BT_MAX_DEVICES];
static int connected_count_ = 0;
static volatile int ble_connected_count_ = 0;
static btstack_context_callback_registration_t le_scan_restart_callbacks_[4];
static btstack_context_callback_registration_t hci_event_logger_register_callback_;
static btstack_packet_callback_registration_t hci_event_callback_registration_;
static uint8_t le_scan_restart_callback_index_ = 0;
static bool hci_event_callback_registered_ = false;
static hci_con_handle_t tembed_le_handle_ = HCI_CON_HANDLE_INVALID;
static hci_con_handle_t tembed_conn_params_requested_handle_ = HCI_CON_HANDLE_INVALID;
static uint64_t next_tembed_adv_log_us_ = 0;
static uint8_t generic_adv_log_count_ = 0;

static constexpr uint16_t TEMBED_LE_CONN_INTERVAL_MIN = 12;     // 15 ms
static constexpr uint16_t TEMBED_LE_CONN_INTERVAL_MAX = 24;     // 30 ms
static constexpr uint16_t TEMBED_LE_CONN_LATENCY = 0;           // no skipped events
static constexpr uint16_t TEMBED_LE_SUPERVISION_TIMEOUT = 256;  // 2.56 s
static constexpr uint64_t TEMBED_ADV_LOG_INTERVAL_US = 1000000;
static constexpr uint8_t GENERIC_ADV_LOG_LIMIT = 12;

static chopper_instance_t* get_instance(uni_hid_device_t* d) {
    return (chopper_instance_t*)&d->platform_data[0];
}

static void format_btaddr(const uint8_t btaddr[6], char out[18]) {
    snprintf(out, 18, "%02X:%02X:%02X:%02X:%02X:%02X", btaddr[0], btaddr[1], btaddr[2], btaddr[3], btaddr[4],
             btaddr[5]);
}

static int mac_nibble(char c) {
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    return -1;
}

static bool btaddr_matches_mac_string(const uint8_t btaddr[6], const char* mac) {
    if (btaddr == NULL || mac == NULL)
        return false;
    if (strlen(mac) != 17)
        return false;

    for (int i = 0; i < 6; ++i) {
        const int offset = i * 3;
        const int hi = mac_nibble(mac[offset]);
        const int lo = mac_nibble(mac[offset + 1]);
        if (hi < 0 || lo < 0)
            return false;
        if (i < 5 && mac[offset + 2] != ':')
            return false;
        if (btaddr[i] != static_cast<uint8_t>((hi << 4) | lo))
            return false;
    }

    return true;
}

static bool is_tembed_btaddr(const uint8_t btaddr[6]) {
    return btaddr_matches_mac_string(btaddr, chopper_config_bluetooth_TEMBED_MAC);
}

static bool is_tembed_device(const uni_hid_device_t* d) {
    return d != NULL && is_tembed_btaddr(d->conn.btaddr);
}

static bool is_tracked_tembed_handle(hci_con_handle_t handle) {
    return tembed_le_handle_ != HCI_CON_HANDLE_INVALID && tembed_le_handle_ == handle;
}

static bool should_defer_discovery_for_active_ble(void) {
    return ble_connected_count_ > 0 && tembed_le_handle_ != HCI_CON_HANDLE_INVALID;
}

static void track_tembed_le_handle(hci_con_handle_t handle, const char* source) {
    if (handle == HCI_CON_HANDLE_INVALID || tembed_le_handle_ == handle)
        return;

    tembed_le_handle_ = handle;
    tembed_conn_params_requested_handle_ = HCI_CON_HANDLE_INVALID;
    ESP_LOGI(TAG, "T-Embed BLE handle tracked: handle=0x%04x source=%s", handle, source ? source : "(unknown)");
}

static const char* hci_disconnect_reason_name(uint8_t reason) {
    switch (reason) {
        case 0x08:
            return "supervision_timeout";
        case 0x13:
            return "remote_user_terminated";
        case 0x16:
            return "local_host_terminated";
        case 0x3e:
            return "connection_failed_to_establish";
        default:
            return "unknown";
    }
}

static void extract_adv_fields(const uint8_t* data, uint8_t data_len, char name[32], uint16_t* appearance) {
    name[0] = '\0';
    *appearance = 0;

    uint8_t offset = 0;
    while (offset < data_len) {
        const uint8_t field_len = data[offset++];
        if (field_len == 0)
            break;
        if (offset + field_len > data_len)
            break;

        const uint8_t field_type = data[offset];
        const uint8_t* value = &data[offset + 1];
        const uint8_t value_len = static_cast<uint8_t>(field_len - 1);
        if ((field_type == 0x08 || field_type == 0x09) && value_len > 0 && name[0] == '\0') {
            const uint8_t copy_len = value_len < 31 ? value_len : 31;
            memcpy(name, value, copy_len);
            name[copy_len] = '\0';
        } else if (field_type == 0x19 && value_len >= 2) {
            *appearance = static_cast<uint16_t>(value[0] | (static_cast<uint16_t>(value[1]) << 8));
        }

        offset = static_cast<uint8_t>(offset + field_len);
    }
}

static void log_le_advertising_report(const uint8_t* packet, uint16_t size) {
    if (packet == NULL || size < 4)
        return;

    uint16_t offset = 3;
    const uint8_t report_count = packet[offset++];
    for (uint8_t report = 0; report < report_count; ++report) {
        if (offset + 10 > size)
            return;

        const uint8_t event_type = packet[offset];
        const uint8_t addr_type = packet[offset + 1];
        bd_addr_t addr;
        for (int i = 0; i < 6; ++i) {
            addr[i] = packet[offset + 2 + (5 - i)];
        }

        const uint8_t data_len = packet[offset + 8];
        if (offset + 10 + data_len > size)
            return;

        char name[32];
        uint16_t appearance = 0;
        extract_adv_fields(&packet[offset + 9], data_len, name, &appearance);

        const int8_t rssi = static_cast<int8_t>(packet[offset + 9 + data_len]);
        const bool is_tembed = is_tembed_btaddr(addr);
        if (is_tembed) {
            const uint64_t now_us = static_cast<uint64_t>(esp_timer_get_time());
            if (now_us >= next_tembed_adv_log_us_) {
                char mac[18];
                format_btaddr(addr, mac);
                ESP_LOGI(TAG,
                         "T-Embed BLE advertisement seen: mac=%s addr_type=%u event_type=0x%02x rssi=%d "
                         "appearance=0x%04x name='%s'",
                         mac, addr_type, event_type, rssi, appearance, name);
                next_tembed_adv_log_us_ = now_us + TEMBED_ADV_LOG_INTERVAL_US;
            }
        } else if (generic_adv_log_count_ < GENERIC_ADV_LOG_LIMIT && (appearance != 0 || name[0] != '\0')) {
            char mac[18];
            format_btaddr(addr, mac);
            ESP_LOGI(
                TAG,
                "BLE advertisement seen: mac=%s addr_type=%u event_type=0x%02x rssi=%d appearance=0x%04x name='%s'",
                mac, addr_type, event_type, rssi, appearance, name);
            generic_adv_log_count_++;
        }

        offset = static_cast<uint16_t>(offset + 10 + data_len);
    }
}

static void log_gap_advertising_report(const uint8_t* packet, uint16_t size) {
    if (packet == NULL || size < 12)
        return;

    bd_addr_t addr;
    gap_event_advertising_report_get_address(packet, addr);
    const uint8_t event_type = gap_event_advertising_report_get_advertising_event_type(packet);
    const uint8_t addr_type = gap_event_advertising_report_get_address_type(packet);
    const uint8_t data_len = gap_event_advertising_report_get_data_length(packet);
    const uint8_t* data = gap_event_advertising_report_get_data(packet);
    if (12U + data_len > size)
        return;
    const int8_t rssi = static_cast<int8_t>(gap_event_advertising_report_get_rssi(packet));

    char name[32];
    uint16_t appearance = 0;
    extract_adv_fields(data, data_len, name, &appearance);

    const bool is_tembed = is_tembed_btaddr(addr);
    if (is_tembed) {
        const uint64_t now_us = static_cast<uint64_t>(esp_timer_get_time());
        if (now_us >= next_tembed_adv_log_us_) {
            char mac[18];
            format_btaddr(addr, mac);
            ESP_LOGI(TAG,
                     "T-Embed BLE advertisement seen: mac=%s addr_type=%u event_type=0x%02x rssi=%d "
                     "appearance=0x%04x name='%s'",
                     mac, addr_type, event_type, rssi, appearance, name);
            next_tembed_adv_log_us_ = now_us + TEMBED_ADV_LOG_INTERVAL_US;
        }
    } else if (generic_adv_log_count_ < GENERIC_ADV_LOG_LIMIT && (appearance != 0 || name[0] != '\0')) {
        char mac[18];
        format_btaddr(addr, mac);
        ESP_LOGI(TAG,
                 "BLE advertisement seen: mac=%s addr_type=%u event_type=0x%02x rssi=%d appearance=0x%04x "
                 "name='%s'",
                 mac, addr_type, event_type, rssi, appearance, name);
        generic_adv_log_count_++;
    }
}

static void log_le_connection_complete(const char* label, uint8_t status, hci_con_handle_t handle, uint8_t role,
                                       uint8_t peer_addr_type, const bd_addr_t peer_addr, uint16_t interval,
                                       uint16_t latency, uint16_t supervision) {
    const bool is_tembed = is_tembed_btaddr(peer_addr);
    if (status == 0 && is_tembed) {
        track_tembed_le_handle(handle, label);
    }

    char peer[18];
    format_btaddr(peer_addr, peer);
    ESP_LOGI(TAG,
             "HCI %s: status=0x%02x handle=0x%04x role=%u peer=%s addr_type=%u interval=%u latency=%u "
             "supervision=%u tembed=%d active_ble=%d",
             label, status, handle, role, peer, peer_addr_type, interval, latency, supervision, is_tembed ? 1 : 0,
             ble_connected_count_);
}

static void log_le_connection_update_complete(const uint8_t* packet) {
    const uint8_t status = hci_subevent_le_connection_update_complete_get_status(packet);
    const hci_con_handle_t handle = hci_subevent_le_connection_update_complete_get_connection_handle(packet);
    const uint16_t interval = hci_subevent_le_connection_update_complete_get_conn_interval(packet);
    const uint16_t latency = hci_subevent_le_connection_update_complete_get_conn_latency(packet);
    const uint16_t supervision = hci_subevent_le_connection_update_complete_get_supervision_timeout(packet);

    ESP_LOGI(TAG,
             "HCI LE conn update: status=0x%02x handle=0x%04x interval=%u latency=%u supervision=%u "
             "tembed=%d active_ble=%d",
             status, handle, interval, latency, supervision, is_tracked_tembed_handle(handle) ? 1 : 0,
             ble_connected_count_);
}

static void chopper_hci_event_handler(uint8_t packet_type, uint16_t channel, uint8_t* packet, uint16_t size) {
    ARG_UNUSED(channel);
    ARG_UNUSED(size);

    if (packet_type != HCI_EVENT_PACKET || packet == NULL)
        return;

    const uint8_t event_type = hci_event_packet_get_type(packet);
    if (event_type == HCI_EVENT_DISCONNECTION_COMPLETE) {
        const uint8_t status = hci_event_disconnection_complete_get_status(packet);
        const hci_con_handle_t handle = hci_event_disconnection_complete_get_connection_handle(packet);
        const uint8_t reason = hci_event_disconnection_complete_get_reason(packet);
        const bool was_tembed = is_tracked_tembed_handle(handle);

        ESP_LOGI(TAG, "HCI disconnect: status=0x%02x handle=0x%04x reason=0x%02x %s tembed=%d active_ble=%d", status,
                 handle, reason, hci_disconnect_reason_name(reason), was_tembed ? 1 : 0, ble_connected_count_);
        if (was_tembed) {
            tembed_le_handle_ = HCI_CON_HANDLE_INVALID;
            tembed_conn_params_requested_handle_ = HCI_CON_HANDLE_INVALID;
        }
        return;
    }

    if (event_type == GAP_EVENT_ADVERTISING_REPORT) {
        log_gap_advertising_report(packet, size);
        return;
    }

    if (event_type != HCI_EVENT_LE_META)
        return;

    switch (hci_event_le_meta_get_subevent_code(packet)) {
        case HCI_SUBEVENT_LE_ADVERTISING_REPORT:
            log_le_advertising_report(packet, size);
            break;
        case HCI_SUBEVENT_LE_CONNECTION_COMPLETE: {
            bd_addr_t peer_addr;
            hci_subevent_le_connection_complete_get_peer_address(packet, peer_addr);
            log_le_connection_complete("LE connected", hci_subevent_le_connection_complete_get_status(packet),
                                       hci_subevent_le_connection_complete_get_connection_handle(packet),
                                       hci_subevent_le_connection_complete_get_role(packet),
                                       hci_subevent_le_connection_complete_get_peer_address_type(packet), peer_addr,
                                       hci_subevent_le_connection_complete_get_conn_interval(packet),
                                       hci_subevent_le_connection_complete_get_conn_latency(packet),
                                       hci_subevent_le_connection_complete_get_supervision_timeout(packet));
            break;
        }
        case HCI_SUBEVENT_LE_CONNECTION_UPDATE_COMPLETE:
            log_le_connection_update_complete(packet);
            break;
        case HCI_SUBEVENT_LE_ENHANCED_CONNECTION_COMPLETE_V1: {
            bd_addr_t peer_addr;
            hci_subevent_le_enhanced_connection_complete_v1_get_peer_addresss(packet, peer_addr);
            log_le_connection_complete(
                "LE enhanced connected v1", hci_subevent_le_enhanced_connection_complete_v1_get_status(packet),
                hci_subevent_le_enhanced_connection_complete_v1_get_connection_handle(packet),
                hci_subevent_le_enhanced_connection_complete_v1_get_role(packet),
                hci_subevent_le_enhanced_connection_complete_v1_get_peer_address_type(packet), peer_addr,
                hci_subevent_le_enhanced_connection_complete_v1_get_conn_interval(packet),
                hci_subevent_le_enhanced_connection_complete_v1_get_conn_latency(packet),
                hci_subevent_le_enhanced_connection_complete_v1_get_supervision_timeout(packet));
            break;
        }
        case HCI_SUBEVENT_LE_ENHANCED_CONNECTION_COMPLETE_V2: {
            bd_addr_t peer_addr;
            hci_subevent_le_enhanced_connection_complete_v2_get_peer_addresss(packet, peer_addr);
            log_le_connection_complete(
                "LE enhanced connected v2", hci_subevent_le_enhanced_connection_complete_v2_get_status(packet),
                hci_subevent_le_enhanced_connection_complete_v2_get_connection_handle(packet),
                hci_subevent_le_enhanced_connection_complete_v2_get_role(packet),
                hci_subevent_le_enhanced_connection_complete_v2_get_peer_address_type(packet), peer_addr,
                hci_subevent_le_enhanced_connection_complete_v2_get_conn_interval(packet),
                hci_subevent_le_enhanced_connection_complete_v2_get_conn_latency(packet),
                hci_subevent_le_enhanced_connection_complete_v2_get_supervision_timeout(packet));
            break;
        }
        default:
            break;
    }
}

static void register_hci_event_logger(void) {
    if (hci_event_callback_registered_)
        return;

    hci_event_callback_registration_.callback = &chopper_hci_event_handler;
    hci_add_event_handler(&hci_event_callback_registration_);
    hci_event_callback_registered_ = true;
    ESP_LOGI(TAG, "HCI event logger registered");
}

static void register_hci_event_logger_callback(void* context) {
    ARG_UNUSED(context);
    register_hci_event_logger();
}

static void schedule_hci_event_logger_registration(void) {
    if (hci_event_callback_registered_)
        return;

    hci_event_logger_register_callback_.callback = &register_hci_event_logger_callback;
    hci_event_logger_register_callback_.context = NULL;
    btstack_run_loop_execute_on_main_thread(&hci_event_logger_register_callback_);
}

static void update_tembed_le_connection_parameters_unsafe(uni_hid_device_t* d) {
    if (d == NULL || d->conn.protocol != UNI_BT_CONN_PROTOCOL_BLE)
        return;
    if (!is_tembed_device(d))
        return;

    track_tembed_le_handle(d->conn.handle, "platform");
    if (tembed_conn_params_requested_handle_ == d->conn.handle) {
        return;
    }

    const int status =
        gap_update_connection_parameters(d->conn.handle, TEMBED_LE_CONN_INTERVAL_MIN, TEMBED_LE_CONN_INTERVAL_MAX,
                                         TEMBED_LE_CONN_LATENCY, TEMBED_LE_SUPERVISION_TIMEOUT);
    if (status == 0) {
        tembed_conn_params_requested_handle_ = d->conn.handle;
        ESP_LOGI(TAG, "T-Embed BLE conn params requested: handle=0x%04x interval=%u-%u latency=%u supervision=%u",
                 d->conn.handle, TEMBED_LE_CONN_INTERVAL_MIN, TEMBED_LE_CONN_INTERVAL_MAX, TEMBED_LE_CONN_LATENCY,
                 TEMBED_LE_SUPERVISION_TIMEOUT);
    } else {
        ESP_LOGE(TAG, "T-Embed BLE conn params update failed: handle=0x%04x status=0x%02x", d->conn.handle, status);
    }
}

static void stop_ble_recovery_le_scan_unsafe(void) {
    // Keep the LE scanner quiet while a Vambrace HOGP link is active, but do
    // not stop BR/EDR inquiry. Joy-Cons need Classic inquiry/autoconnect, and
    // stopping Bluepad's full scan state here blocks them from registering.
    uni_bt_le_scan_stop();
}

static void start_classic_discovery_with_le_quiet_unsafe(const char* source) {
    uni_bt_allow_incoming_connections(true);
    ESP_LOGI(TAG, "starting Classic discovery with LE scan stopped: source=%s active_ble=%d tembed_handle=0x%04x",
             source ? source : "(unknown)", ble_connected_count_, tembed_le_handle_);
    uni_bt_start_scanning_and_autoconnect_unsafe();
    uni_bt_le_scan_stop();
}

static void restart_le_scan_unsafe(void) {
    if (should_defer_discovery_for_active_ble()) {
        ESP_LOGI(TAG,
                 "LE scan restart skipped; allowing Classic discovery while T-Embed BLE active handle=0x%04x "
                 "active_ble=%d",
                 tembed_le_handle_, ble_connected_count_);
        start_classic_discovery_with_le_quiet_unsafe("le_restart_active_ble");
        return;
    }
    ESP_LOGI(TAG, "restarting LE scan and Classic discovery for controller recovery");
    uni_bt_start_scanning_and_autoconnect_unsafe();
    uni_bt_le_scan_start();
}

static void start_full_discovery_or_pause_for_ble_unsafe(const char* source) {
    if (should_defer_discovery_for_active_ble()) {
        ESP_LOGI(TAG,
                 "full BT discovery split; keeping LE quiet while allowing Classic: T-Embed BLE active "
                 "handle=0x%04x active_ble=%d source=%s",
                 tembed_le_handle_, ble_connected_count_, source ? source : "(unknown)");
        start_classic_discovery_with_le_quiet_unsafe(source);
        return;
    }

    uni_bt_start_scanning_and_autoconnect_unsafe();
}

static void restart_le_scan_callback(void* context) {
    ARG_UNUSED(context);
    uni_bt_allow_incoming_connections(true);
    restart_le_scan_unsafe();
}

static void start_classic_discovery_with_le_quiet_callback(void* context) {
    start_classic_discovery_with_le_quiet_unsafe(static_cast<const char*>(context));
}

static void schedule_scan_control_callback(void (*callback)(void*), void* context) {
    btstack_context_callback_registration_t* registration =
        &le_scan_restart_callbacks_[le_scan_restart_callback_index_];
    le_scan_restart_callback_index_ =
        static_cast<uint8_t>((le_scan_restart_callback_index_ + 1) %
                             (sizeof(le_scan_restart_callbacks_) / sizeof(le_scan_restart_callbacks_[0])));
    registration->callback = callback;
    registration->context = context;
    btstack_run_loop_execute_on_main_thread(registration);
}

static void clear_controller_snapshot_locked(int slot) {
    memset(&controllers_[slot], 0, sizeof(controllers_[slot]));
    controllers_[slot].hid_index = -1;
}

static int8_t find_connected_slot_by_mac_locked(const uint8_t btaddr[6]) {
    for (int i = 0; i < CHOPPER_BT_MAX_DEVICES; i++) {
        if (!controllers_[i].connected)
            continue;
        if (memcmp(controllers_[i].btaddr, btaddr, sizeof(controllers_[i].btaddr)) == 0)
            return (int8_t)i;
    }
    return -1;
}

// Find the first free slot; returns -1 if none.
static int8_t find_free_slot(void) {
    for (int i = 0; i < CHOPPER_BT_MAX_DEVICES; i++) {
        if (!controllers_[i].connected)
            return (int8_t)i;
    }
    return -1;
}

// --------------------------------------------------------------------------
// Platform callbacks (run on Bluetooth task / CPU0)
// --------------------------------------------------------------------------

static void chopper_init(int argc, const char** argv) {
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    controller_mutex_ = xSemaphoreCreateMutex();
    connected_count_ = 0;
    ble_connected_count_ = 0;
    le_scan_restart_callback_index_ = 0;
    tembed_le_handle_ = HCI_CON_HANDLE_INVALID;
    tembed_conn_params_requested_handle_ = HCI_CON_HANDLE_INVALID;
    for (int i = 0; i < CHOPPER_BT_MAX_DEVICES; ++i) {
        clear_controller_snapshot_locked(i);
    }
}

static void chopper_on_init_complete(void) {
    ESP_LOGI(TAG, "Bluetooth init complete");
    schedule_hci_event_logger_registration();

    // Set LE Create Connection defaults before Bluepad32 calls gap_connect().
    gap_set_connection_parameters(0x0060, 0x0030, TEMBED_LE_CONN_INTERVAL_MIN, TEMBED_LE_CONN_INTERVAL_MAX,
                                  TEMBED_LE_CONN_LATENCY, TEMBED_LE_SUPERVISION_TIMEOUT, 0, 0);

    // Bluetooth Classic "Sniff mode" is a low-power link policy where a controller
    // only listens/transmits at scheduled intervals. That saves battery, but it also
    // adds wakeup scheduling jitter and can increase latency under load.
    //
    // For Chopper, controllers are used for real-time robot driving / actuation, so
    // input determinism is more important than controller battery savings. In testing,
    // allowing Sniff caused multi-controller instability symptoms ("connected but stale"
    // data / apparent silent drop). Keeping links in active mode removed those issues.
    //
    // Policy here:
    // - Keep ROLE_SWITCH enabled (helps interoperability with some controllers).
    // - Disable SNIFF by not including LM_LINK_POLICY_ENABLE_SNIFF_MODE.
    //
    // Thread-safety note:
    // This callback runs on the BTstack thread, so calling BTstack APIs directly is safe.
    gap_set_default_link_policy_settings(LM_LINK_POLICY_ENABLE_ROLE_SWITCH);

    // Safe to call "unsafe" functions here since we're on the BT thread.
    // Start scanning for controllers and allow incoming connections.
    ESP_LOGI(TAG, "starting initial BT discovery");
    uni_bt_start_scanning_and_autoconnect_unsafe();
    uni_bt_allow_incoming_connections(true);
}

static uni_error_t chopper_on_device_discovered(bd_addr_t addr, const char* name, uint16_t cod, uint8_t rssi) {
    // Filter out keyboards — Chopper only uses gamepads
    if (((cod & UNI_BT_COD_MINOR_MASK) & UNI_BT_COD_MINOR_KEYBOARD) == UNI_BT_COD_MINOR_KEYBOARD) {
        ESP_LOGI(TAG, "ignoring keyboard device");
        return UNI_ERROR_IGNORE_DEVICE;
    }

    ESP_LOGI(TAG, "device discovered, MAC=%02X:%02X:%02X:%02X:%02X:%02X name='%s', cod=0x%04x, rssi=%d", addr[0],
             addr[1], addr[2], addr[3], addr[4], addr[5], name ? name : "(null)", cod, rssi);

    // For BLE HOGP, Bluepad32 creates the HID device record after this platform
    // callback returns, then its BLE connect path stops LE scanning. Stopping
    // Chopper's full discovery stack here can race that setup and leave the SM
    // pairing event without a matching device record.
    if (is_tembed_btaddr(addr)) {
        ESP_LOGI(TAG, "T-Embed discovered; deferring scan stop to Bluepad BLE connect path");
    } else {
        // Serialize Classic controller handshakes to reduce inquiry churn during
        // L2CAP setup.
        uni_bt_stop_scanning_unsafe();
    }

    return UNI_ERROR_SUCCESS;
}

static void chopper_on_device_connected(uni_hid_device_t* d) {
    chopper_instance_t* ins = get_instance(d);
    ins->slot_idx = -1;  // Not assigned until ready
    memcpy(ins->btaddr, d->conn.btaddr, sizeof(ins->btaddr));
    ins->controller_type = d->controller_type;

    // Access btaddr directly from the connection struct (avoids extern "C" issue
    // with uni_bt_conn.h which lacks C++ linkage guards)
    const uint8_t* addr = d->conn.btaddr;
    ESP_LOGI(TAG, "device connected, MAC=%02X:%02X:%02X:%02X:%02X:%02X", addr[0], addr[1], addr[2], addr[3], addr[4],
             addr[5]);

    if (d->conn.protocol != UNI_BT_CONN_PROTOCOL_BLE) {
        // Keep classic inquiry paused while setup for a BR/EDR controller finishes.
        uni_bt_stop_scanning_unsafe();
    } else {
        ble_connected_count_ = ble_connected_count_ + 1;
        // Keep LE scan quiet while a HOGP connection is being serviced. Continuous
        // scanning while connected competes with HID notifications on ESP32 and
        // showed up as missed buttons / stale reports under encoder load.
        ESP_LOGI(TAG, "BLE controller connected; stopping LE scan: handle=0x%04x active_ble=%d", d->conn.handle,
                 ble_connected_count_);
        if (is_tembed_device(d)) {
            track_tembed_le_handle(d->conn.handle, "platform_connect");
        }
        stop_ble_recovery_le_scan_unsafe();
    }
}

static void chopper_on_device_disconnected(uni_hid_device_t* d) {
    chopper_instance_t* ins = get_instance(d);
    const int hid_idx = uni_hid_device_get_idx_for_instance(d);
    const uint8_t* addr = d->conn.btaddr;
    const bool was_ble = d->conn.protocol == UNI_BT_CONN_PROTOCOL_BLE;

    if (ins->slot_idx >= 0 && ins->slot_idx < CHOPPER_BT_MAX_DEVICES) {
        xSemaphoreTake(controller_mutex_, portMAX_DELAY);
        clear_controller_snapshot_locked(ins->slot_idx);
        connected_count_--;
        if (connected_count_ < 0)
            connected_count_ = 0;
        xSemaphoreGive(controller_mutex_);

        ESP_LOGI(TAG, "device disconnected, hid_idx=%d slot=%d mac=%02X:%02X:%02X:%02X:%02X:%02X count=%d", hid_idx,
                 ins->slot_idx, addr[0], addr[1], addr[2], addr[3], addr[4], addr[5], connected_count_);
    } else {
        ESP_LOGI(TAG, "device disconnected before ready, hid_idx=%d mac=%02X:%02X:%02X:%02X:%02X:%02X count=%d",
                 hid_idx, addr[0], addr[1], addr[2], addr[3], addr[4], addr[5], connected_count_);
    }

    ins->slot_idx = -1;

    if (was_ble) {
        if (ble_connected_count_ > 0)
            ble_connected_count_ = ble_connected_count_ - 1;
        else
            ble_connected_count_ = 0;
        if (is_tembed_device(d) && is_tracked_tembed_handle(d->conn.handle)) {
            tembed_le_handle_ = HCI_CON_HANDLE_INVALID;
            tembed_conn_params_requested_handle_ = HCI_CON_HANDLE_INVALID;
        }
        restart_le_scan_unsafe();
    } else {
        start_full_discovery_or_pause_for_ble_unsafe("classic_disconnect");
    }
    uni_bt_allow_incoming_connections(true);
}

static uni_error_t chopper_on_device_ready(uni_hid_device_t* d) {
    chopper_instance_t* ins = get_instance(d);
    const int hid_idx = uni_hid_device_get_idx_for_instance(d);

    int8_t slot = find_free_slot();
    if (slot < 0) {
        ESP_LOGE(TAG, "no free controller slot");
        return UNI_ERROR_NO_SLOTS;
    }

    ins->slot_idx = slot;

    xSemaphoreTake(controller_mutex_, portMAX_DELAY);
    controllers_[slot].connected = true;
    memset(&controllers_[slot].gamepad, 0, sizeof(uni_gamepad_t));
    controllers_[slot].battery = 0;
    memcpy(controllers_[slot].btaddr, ins->btaddr, sizeof(controllers_[slot].btaddr));
    controllers_[slot].controller_type = d->controller_type;
    controllers_[slot].last_report_time_us = 0;
    controllers_[slot].hid_index = static_cast<int8_t>(hid_idx);
    connected_count_++;
    xSemaphoreGive(controller_mutex_);

    ESP_LOGI(TAG, "device ready, slot=%d", slot);

    // Set player LEDs to indicate slot number (if the controller supports it)
    if (d->report_parser.set_player_leds != NULL) {
        d->report_parser.set_player_leds(d, BIT(slot));
    }

    // Keep LE scan quiet while the BLE controller is live. If the T-Embed goes
    // silent without a proper disconnect, BluepadInputNode forces a disconnect
    // after the report-quiet grace period, and the disconnect callback restarts
    // LE scanning.
    if (d->conn.protocol == UNI_BT_CONN_PROTOCOL_BLE) {
        update_tembed_le_connection_parameters_unsafe(d);
        ESP_LOGI(TAG, "BLE controller ready; keeping LE scan stopped: handle=0x%04x active_ble=%d", d->conn.handle,
                 ble_connected_count_);
        stop_ble_recovery_le_scan_unsafe();
    } else {
        start_full_discovery_or_pause_for_ble_unsafe("classic_ready");
    }
    uni_bt_allow_incoming_connections(true);

    return UNI_ERROR_SUCCESS;
}

static void chopper_on_controller_data(uni_hid_device_t* d, uni_controller_t* ctl) {
    chopper_instance_t* ins = get_instance(d);
    const int hid_idx = uni_hid_device_get_idx_for_instance(d);

    // Only handle gamepads
    if (ctl->klass != UNI_CONTROLLER_CLASS_GAMEPAD) {
        return;
    }

    xSemaphoreTake(controller_mutex_, portMAX_DELAY);

    int8_t slot = ins->slot_idx;
    const bool slot_in_range = (slot >= 0 && slot < CHOPPER_BT_MAX_DEVICES);
    const bool slot_matches_mac =
        slot_in_range && controllers_[slot].connected &&
        memcmp(controllers_[slot].btaddr, d->conn.btaddr, sizeof(controllers_[slot].btaddr)) == 0;

    if (!slot_matches_mac) {
        int8_t resolved_slot = find_connected_slot_by_mac_locked(d->conn.btaddr);
        if (resolved_slot >= 0) {
            ESP_LOGD(TAG, "slot mismatch repaired: hid_idx=%d old_slot=%d new_slot=%d", hid_idx, slot, resolved_slot);
            ins->slot_idx = resolved_slot;
            slot = resolved_slot;
        } else {
            ESP_LOGE(TAG, "dropping report, unresolved slot: hid_idx=%d slot=%d connected_count=%d", hid_idx, slot,
                     connected_count_);
            xSemaphoreGive(controller_mutex_);
            return;
        }
    }

    const uint64_t now_us = static_cast<uint64_t>(esp_timer_get_time());
    const uint64_t previous_report_us = controllers_[slot].last_report_time_us;

    controllers_[slot].gamepad = ctl->gamepad;
    controllers_[slot].battery = ctl->battery;
    controllers_[slot].controller_type = d->controller_type;
    controllers_[slot].last_report_gap_us =
        (previous_report_us > 0 && now_us > previous_report_us)
            ? static_cast<uint32_t>((now_us - previous_report_us) > 0xffffffffULL ? 0xffffffffULL
                                                                                  : (now_us - previous_report_us))
            : 0;
    controllers_[slot].last_report_time_us = now_us;
    controllers_[slot].report_count++;

    xSemaphoreGive(controller_mutex_);
}

static void chopper_on_oob_event(uni_platform_oob_event_t event, void* data) {
    switch (event) {
        case UNI_PLATFORM_OOB_GAMEPAD_SYSTEM_BUTTON: {
            uni_hid_device_t* d = (uni_hid_device_t*)data;
            if (d == NULL) {
                ESP_LOGE(TAG, "OOB system button event with NULL device");
                return;
            }
            ESP_LOGI(TAG, "system button pressed on device %p", d);
            break;
        }
        case UNI_PLATFORM_OOB_BLUETOOTH_ENABLED:
            ESP_LOGI(TAG, "Bluetooth enabled: %d", (bool)(data));
            break;
        default:
            ESP_LOGI(TAG, "unknown OOB event: 0x%04x", event);
            break;
    }
}

static const uni_property_t* chopper_get_property(uni_property_idx_t idx) {
    ARG_UNUSED(idx);
    // No custom properties; fall through to Bluepad32 defaults
    return NULL;
}

// --------------------------------------------------------------------------
// Platform struct
// --------------------------------------------------------------------------

struct uni_platform* get_chopper_platform(void) {
    // Field order must match struct uni_platform declaration in uni_platform.h
    static struct uni_platform plat = {
        .name = "Chopper",
        .init = chopper_init,
        .on_init_complete = chopper_on_init_complete,
        .on_device_discovered = chopper_on_device_discovered,
        .on_device_connected = chopper_on_device_connected,
        .on_device_disconnected = chopper_on_device_disconnected,
        .on_device_ready = chopper_on_device_ready,
        .on_controller_data = chopper_on_controller_data,
        .get_property = chopper_get_property,
        .on_oob_event = chopper_on_oob_event,
    };
    return &plat;
}

// --------------------------------------------------------------------------
// Public API (thread-safe, callable from any task)
// --------------------------------------------------------------------------

int chopper_bt_get_gamepad(int slot_index, chopper_gamepad_data_t* out_data) {
    if (slot_index < 0 || slot_index >= CHOPPER_BT_MAX_DEVICES || !out_data)
        return -1;
    if (!controller_mutex_)
        return -1;

    // Short bounded wait to reduce missed reads during connect/setup bursts.
    // This keeps executor jitter bounded while making slot transitions reliable.
    if (xSemaphoreTake(controller_mutex_, pdMS_TO_TICKS(2)) != pdTRUE) {
        return -1;
    }
    *out_data = controllers_[slot_index];
    xSemaphoreGive(controller_mutex_);

    return 0;
}

int chopper_bt_disconnect_gamepad_with_reason(int slot_index, const char* reason) {
    if (slot_index < 0 || slot_index >= CHOPPER_BT_MAX_DEVICES)
        return -1;
    if (!controller_mutex_)
        return -1;

    int8_t hid_idx = -1;
    bool connected = false;
    uint8_t btaddr[6] = {};
    uint64_t last_report_time_us = 0;
    if (xSemaphoreTake(controller_mutex_, pdMS_TO_TICKS(2)) != pdTRUE) {
        return -1;
    }
    if (controllers_[slot_index].connected) {
        connected = true;
        hid_idx = controllers_[slot_index].hid_index;
        memcpy(btaddr, controllers_[slot_index].btaddr, sizeof(btaddr));
        last_report_time_us = controllers_[slot_index].last_report_time_us;
    }
    xSemaphoreGive(controller_mutex_);

    if (hid_idx < 0) {
        ESP_LOGI(TAG, "BT disconnect request ignored: slot=%d connected=%d hid_idx=%d reason=%s", slot_index,
                 connected ? 1 : 0, hid_idx, reason ? reason : "(none)");
        return -1;
    }

    char mac[18];
    format_btaddr(btaddr, mac);
    const uint64_t now_us = static_cast<uint64_t>(esp_timer_get_time());
    const uint64_t last_report_age_ms =
        (last_report_time_us > 0 && now_us >= last_report_time_us) ? ((now_us - last_report_time_us) / 1000ULL) : 0;
    ESP_LOGI(TAG, "requesting BT disconnect: slot=%d hid_idx=%d mac=%s reason=%s last_report_age=%llu ms", slot_index,
             hid_idx, mac, reason ? reason : "(none)", static_cast<unsigned long long>(last_report_age_ms));

    uni_bt_disconnect_device_safe(hid_idx);
    return 0;
}

int chopper_bt_disconnect_gamepad(int slot_index) {
    return chopper_bt_disconnect_gamepad_with_reason(slot_index, "unspecified");
}

void chopper_bt_restart_discovery(void) {
    uni_bt_allow_incoming_connections(true);
    if (should_defer_discovery_for_active_ble()) {
        ESP_LOGI(TAG,
                 "full BT discovery restart split; allowing Classic while keeping LE quiet: T-Embed BLE active "
                 "handle=0x%04x active_ble=%d",
                 tembed_le_handle_, ble_connected_count_);
        schedule_scan_control_callback(&start_classic_discovery_with_le_quiet_callback,
                                       const_cast<char*>("restart_discovery_active_ble"));
        return;
    }
    ESP_LOGI(TAG, "full BT discovery restart requested");
    uni_bt_stop_scanning_safe();
    uni_bt_start_scanning_and_autoconnect_safe();
}

void chopper_bt_restart_le_discovery(void) {
    uni_bt_allow_incoming_connections(true);
    if (should_defer_discovery_for_active_ble()) {
        ESP_LOGI(TAG,
                 "LE discovery restart ignored; allowing Classic while T-Embed BLE active handle=0x%04x active_ble=%d",
                 tembed_le_handle_, ble_connected_count_);
        schedule_scan_control_callback(&start_classic_discovery_with_le_quiet_callback,
                                       const_cast<char*>("le_discovery_active_ble"));
        return;
    }
    ESP_LOGI(TAG, "LE discovery restart requested");
    schedule_scan_control_callback(&restart_le_scan_callback, NULL);
}

int chopper_bt_connected_count(void) {
    if (!controller_mutex_)
        return 0;

    if (xSemaphoreTake(controller_mutex_, pdMS_TO_TICKS(2)) != pdTRUE) {
        return 0;
    }
    int count = connected_count_;
    xSemaphoreGive(controller_mutex_);

    return count;
}

#endif  // ESP_PLATFORM
