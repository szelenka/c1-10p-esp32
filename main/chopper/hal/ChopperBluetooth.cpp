// SPDX-License-Identifier: Apache-2.0
// Native Bluepad32 platform for Chopper — replaces Arduino platform.

#include "sdkconfig.h"

#ifdef ESP_PLATFORM

#include "chopper/hal/ChopperBluetooth.h"

#include <string.h>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <esp_timer.h>

#include <uni.h>

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
static chopper_gamepad_data_t controllers_[CHOPPER_BT_MAX_DEVICES];
static int connected_count_ = 0;


static chopper_instance_t* get_instance(uni_hid_device_t* d) {
    return (chopper_instance_t*)&d->platform_data[0];
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
    memset(controllers_, 0, sizeof(controllers_));
}

static void chopper_on_init_complete(void) {
    logi("Chopper: Bluetooth init complete\n");

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
    uni_bt_start_scanning_and_autoconnect_unsafe();
    uni_bt_allow_incoming_connections(true);
}

static uni_error_t chopper_on_device_discovered(bd_addr_t addr, const char* name,
                                                uint16_t cod, uint8_t rssi) {
    // Filter out keyboards — Chopper only uses gamepads
    if (((cod & UNI_BT_COD_MINOR_MASK) & UNI_BT_COD_MINOR_KEYBOARD) == UNI_BT_COD_MINOR_KEYBOARD) {
        logi("Chopper: ignoring keyboard device\n");
        return UNI_ERROR_IGNORE_DEVICE;
    }

    logi("Chopper: device discovered, name='%s', cod=0x%04x, rssi=%d\n",
         name ? name : "(null)", cod, rssi);

    // Serialize handshakes to reduce inquiry churn during L2CAP setup.
    uni_bt_stop_scanning_unsafe();

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
    logi("Chopper: device connected, MAC=%02X:%02X:%02X:%02X:%02X:%02X\n",
         addr[0], addr[1], addr[2], addr[3], addr[4], addr[5]);

    // Keep scan paused while setup for this controller finishes.
    uni_bt_stop_scanning_unsafe();
}

static void chopper_on_device_disconnected(uni_hid_device_t* d) {
    chopper_instance_t* ins = get_instance(d);
    const int hid_idx = uni_hid_device_get_idx_for_instance(d);
    const uint8_t* addr = d->conn.btaddr;

    if (ins->slot_idx >= 0 && ins->slot_idx < CHOPPER_BT_MAX_DEVICES) {
        xSemaphoreTake(controller_mutex_, portMAX_DELAY);
        controllers_[ins->slot_idx].connected = false;
        memset(&controllers_[ins->slot_idx].gamepad, 0, sizeof(uni_gamepad_t));
        controllers_[ins->slot_idx].battery = 0;
        memset(controllers_[ins->slot_idx].btaddr, 0, sizeof(controllers_[ins->slot_idx].btaddr));
        controllers_[ins->slot_idx].controller_type = 0;
        controllers_[ins->slot_idx].last_report_time_us = 0;
        connected_count_--;
        if (connected_count_ < 0) connected_count_ = 0;
        xSemaphoreGive(controller_mutex_);

        logi("Chopper: device disconnected, hid_idx=%d slot=%d mac=%02X:%02X:%02X:%02X:%02X:%02X count=%d\n",
             hid_idx, ins->slot_idx,
             addr[0], addr[1], addr[2], addr[3], addr[4], addr[5],
             connected_count_);
    }

    ins->slot_idx = -1;

    uni_bt_start_scanning_and_autoconnect_unsafe();
    uni_bt_allow_incoming_connections(true);
}

static uni_error_t chopper_on_device_ready(uni_hid_device_t* d) {
    chopper_instance_t* ins = get_instance(d);

    int8_t slot = find_free_slot();
    if (slot < 0) {
        loge("Chopper: no free controller slot\n");
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
    connected_count_++;
    xSemaphoreGive(controller_mutex_);

    logi("Chopper: device ready, slot=%d\n", slot);

    // Set player LEDs to indicate slot number (if the controller supports it)
    if (d->report_parser.set_player_leds != NULL) {
        d->report_parser.set_player_leds(d, BIT(slot));
    }

    uni_bt_start_scanning_and_autoconnect_unsafe();
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
        slot_in_range &&
        controllers_[slot].connected &&
        memcmp(controllers_[slot].btaddr, d->conn.btaddr, sizeof(controllers_[slot].btaddr)) == 0;

    if (!slot_matches_mac) {
        int8_t resolved_slot = find_connected_slot_by_mac_locked(d->conn.btaddr);
        if (resolved_slot >= 0) {
            logd("Chopper: slot mismatch repaired: hid_idx=%d old_slot=%d new_slot=%d",
                 hid_idx, slot, resolved_slot);
            ins->slot_idx = resolved_slot;
            slot = resolved_slot;
        } else {
            loge("Chopper: dropping report, unresolved slot: hid_idx=%d slot=%d connected_count=%d",
                 hid_idx, slot, connected_count_);
            xSemaphoreGive(controller_mutex_);
            return;
        }
    }

    controllers_[slot].gamepad = ctl->gamepad;
    controllers_[slot].battery = ctl->battery;
    controllers_[slot].controller_type = d->controller_type;
    controllers_[slot].last_report_time_us = static_cast<uint64_t>(esp_timer_get_time());

    xSemaphoreGive(controller_mutex_);
}

static void chopper_on_oob_event(uni_platform_oob_event_t event, void* data) {
    switch (event) {
        case UNI_PLATFORM_OOB_GAMEPAD_SYSTEM_BUTTON: {
            uni_hid_device_t* d = (uni_hid_device_t*)data;
            if (d == NULL) {
                loge("Chopper: OOB system button event with NULL device\n");
                return;
            }
            logi("Chopper: system button pressed on device %p\n", d);
            break;
        }
        case UNI_PLATFORM_OOB_BLUETOOTH_ENABLED:
            logi("Chopper: Bluetooth enabled: %d\n", (bool)(data));
            break;
        default:
            logi("Chopper: unknown OOB event: 0x%04x\n", event);
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

#endif // ESP_PLATFORM
