// SPDX-License-Identifier: Apache-2.0
// Native Bluepad32 platform for Chopper — replaces Arduino platform.

#include "sdkconfig.h"

#ifdef ESP_PLATFORM

#include "chopper/hal/ChopperBluetooth.h"

#include <string.h>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

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
} chopper_instance_t;

_Static_assert(sizeof(chopper_instance_t) < HID_DEVICE_MAX_PLATFORM_DATA,
               "chopper_instance_t too big for platform_data");

static SemaphoreHandle_t controller_mutex_ = NULL;
static chopper_gamepad_data_t controllers_[CHOPPER_BT_MAX_DEVICES];
static int connected_count_ = 0;

static chopper_instance_t* get_instance(uni_hid_device_t* d) {
    return (chopper_instance_t*)&d->platform_data[0];
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

    return UNI_ERROR_SUCCESS;
}

static void chopper_on_device_connected(uni_hid_device_t* d) {
    chopper_instance_t* ins = get_instance(d);
    ins->slot_idx = -1;  // Not assigned until ready

    // Access btaddr directly from the connection struct (avoids extern "C" issue
    // with uni_bt_conn.h which lacks C++ linkage guards)
    const uint8_t* addr = d->conn.btaddr;
    logi("Chopper: device connected, MAC=%02X:%02X:%02X:%02X:%02X:%02X\n",
         addr[0], addr[1], addr[2], addr[3], addr[4], addr[5]);
}

static void chopper_on_device_disconnected(uni_hid_device_t* d) {
    chopper_instance_t* ins = get_instance(d);

    if (ins->slot_idx >= 0 && ins->slot_idx < CHOPPER_BT_MAX_DEVICES) {
        xSemaphoreTake(controller_mutex_, portMAX_DELAY);
        controllers_[ins->slot_idx].connected = false;
        memset(&controllers_[ins->slot_idx].gamepad, 0, sizeof(uni_gamepad_t));
        controllers_[ins->slot_idx].battery = 0;
        connected_count_--;
        if (connected_count_ < 0) connected_count_ = 0;
        xSemaphoreGive(controller_mutex_);

        logi("Chopper: device disconnected, slot=%d\n", ins->slot_idx);
    }

    ins->slot_idx = -1;
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
    connected_count_++;
    xSemaphoreGive(controller_mutex_);

    logi("Chopper: device ready, slot=%d\n", slot);

    // Set player LEDs to indicate slot number (if the controller supports it)
    if (d->report_parser.set_player_leds != NULL) {
        d->report_parser.set_player_leds(d, BIT(slot));
    }

    return UNI_ERROR_SUCCESS;
}

static void chopper_on_controller_data(uni_hid_device_t* d, uni_controller_t* ctl) {
    chopper_instance_t* ins = get_instance(d);

    if (ins->slot_idx < 0 || ins->slot_idx >= CHOPPER_BT_MAX_DEVICES)
        return;

    // Only handle gamepads
    if (ctl->klass != UNI_CONTROLLER_CLASS_GAMEPAD)
        return;

    xSemaphoreTake(controller_mutex_, portMAX_DELAY);
    controllers_[ins->slot_idx].gamepad = ctl->gamepad;
    controllers_[ins->slot_idx].battery = ctl->battery;
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

    xSemaphoreTake(controller_mutex_, portMAX_DELAY);
    *out_data = controllers_[slot_index];
    xSemaphoreGive(controller_mutex_);

    return 0;
}

int chopper_bt_connected_count(void) {
    if (!controller_mutex_)
        return 0;

    xSemaphoreTake(controller_mutex_, portMAX_DELAY);
    int count = connected_count_;
    xSemaphoreGive(controller_mutex_);

    return count;
}

#endif // ESP_PLATFORM
