#pragma once

/**
 * Native Bluepad32 platform for the Chopper project.
 *
 * Replaces the Arduino-based Bluepad32 platform with a pure ESP-IDF
 * implementation. Provides thread-safe access to controller data
 * between the Bluetooth task (CPU0) and the application task (CPU1).
 *
 * Usage in main.c:
 *   uni_platform_set_custom(get_chopper_platform());
 *   uni_init(0, NULL);
 *
 * Usage from C++ application code:
 *   chopper_gamepad_data_t gp;
 *   if (chopper_bt_get_gamepad(0, &gp) == 0) { ... }
 */

#ifdef ESP_PLATFORM

#include <stdint.h>
#include <stdbool.h>

#include "controller/uni_gamepad.h"

#ifdef __cplusplus
extern "C" {
#endif

// Forward-declare Bluepad32 types
struct uni_platform;

/**
 * Snapshot of controller state, safe to read from any task.
 */
typedef struct {
    bool connected;                ///< True if a controller is connected in this slot
    uni_gamepad_t gamepad;         ///< Gamepad axes/buttons (zeroed if not connected)
    uint8_t battery;               ///< Battery level (0=empty, 254=full, 255=N/A)
    uint8_t btaddr[6];             ///< Controller MAC address (raw bytes)
    uint16_t controller_type;      ///< Bluepad32 controller type
    uint64_t last_report_time_us;  ///< esp_timer timestamp of last real controller report
} chopper_gamepad_data_t;

/**
 * Maximum number of simultaneous controllers.
 * Matches CONFIG_BLUEPAD32_MAX_DEVICES (default 4).
 */
#ifndef CHOPPER_BT_MAX_DEVICES
#define CHOPPER_BT_MAX_DEVICES 4
#endif

/**
 * Returns the native Chopper Bluepad32 platform.
 * Pass this to uni_platform_set_custom() before uni_init().
 */
struct uni_platform* get_chopper_platform(void);

/**
 * Read the current gamepad state for a given controller slot.
 * Thread-safe (uses a FreeRTOS mutex internally).
 *
 * @param slot_index  Controller index (0 to CHOPPER_BT_MAX_DEVICES-1).
 * @param out_data    Output: filled with the current state.
 * @return 0 on success, -1 if slot_index is out of range or mutex unavailable.
 */
int chopper_bt_get_gamepad(int slot_index, chopper_gamepad_data_t* out_data);

/**
 * Returns the number of currently connected controllers.
 * Thread-safe.
 */
int chopper_bt_connected_count(void);

#ifdef __cplusplus
}
#endif

#endif  // ESP_PLATFORM
