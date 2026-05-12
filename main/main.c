// SPDX-License-Identifier: Apache-2.0
// Copyright 2019 Ricardo Quesada
// http://retro.moe/unijoysticle2

#include "sdkconfig.h"

#include <stddef.h>
#include <nvs_flash.h>

// BTstack related
#include <btstack_port_esp32.h>
#include <btstack_run_loop.h>
#include <btstack_stdio_esp32.h>

// Bluepad32 related
#include <uni.h>

// Native Chopper Bluepad32 platform (runtime path)
#include "chopper/hal/ChopperBluetooth.h"
#ifdef CHOPPER_VALIDATION_RUNTIME
// Upstream-like Bluepad32 Arduino platform (isolation path)
#include "arduino_platform.h"
#endif

// Controller MAC addresses from HardwareConfig
#include "chopper/config/HardwareConfig.h"

// Implemented in C++ runtime bootstrap.
int chopper_runtime_start(void);

int app_main(void) {
    // hci_dump_open(NULL, HCI_DUMP_STDOUT);

    // Bluepad32 allowlist / link keys rely on NVS; initialize it before BT stack.
    esp_err_t nvs_err = nvs_flash_init();
    if (nvs_err == ESP_ERR_NVS_NO_FREE_PAGES || nvs_err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    } else if (nvs_err != ESP_OK) {
        return 1;
    }

// Don't use BTstack buffered UART. It conflicts with the console.
#ifdef CONFIG_ESP_CONSOLE_UART
#ifndef CONFIG_BLUEPAD32_USB_CONSOLE_ENABLE
    btstack_stdio_init();
#endif  // CONFIG_BLUEPAD32_USB_CONSOLE_ENABLE
#endif  // CONFIG_ESP_CONSOLE_UART

    // Configure BTstack for ESP32 VHCI Controller
    btstack_init();

    // hci_dump_init(hci_dump_embedded_stdout_get_instance());

#ifdef CHOPPER_VALIDATION_RUNTIME
    // Validation isolation mode:
    // Match upstream bluepad32_arduino template behavior as closely as possible.
    uni_platform_set_custom(get_arduino_platform());
    uni_init(0 /* argc */, NULL /* argv */);

    // Does not return.
    btstack_run_loop_execute();
    return 0;
#else
    // Must be called before uni_init()
    uni_platform_set_custom(get_chopper_platform());

    // Init Bluepad32.
    uni_init(0 /* argc */, NULL /* argv */);

    // Controller MAC addresses from HardwareConfig.h
    static const char* mac_addrs[] = {
        chopper_config_bluetooth_DRIVE_MAC,
        chopper_config_bluetooth_DOME_MAC,
        chopper_config_bluetooth_ANIMATE_MAC,
        chopper_config_bluetooth_CAMERA_MAC,
        chopper_config_bluetooth_TEMBED_MAC,
    };

    if (chopper_config_bluetooth_FORGET_ON_STARTUP)
    {
        uni_bt_allowlist_remove_all();
        uni_bt_del_keys_safe();
    }

    // Loop through the addresses and add them to the allowlist
    for (size_t i = 0; i < sizeof(mac_addrs) / sizeof(mac_addrs[0]); i++)
    {
        bd_addr_t controller_addr;
        // Parse human-readable Bluetooth address.
        if (!sscanf_bd_addr(mac_addrs[i], controller_addr)) {
            printf("Failed to parse controller MAC: %s\n", mac_addrs[i]);
            return 1;
        }

        // Notice that this address will be added in the Non-volatile-storage (NVS).
        // If the device reboots, the address will still be stored.
        // Adding a duplicate value will do nothing.
        // Allowlist entries persist in NVS; duplicate adds are ignored.
        if (!uni_bt_allowlist_add_addr(controller_addr) &&
            !uni_bt_allowlist_is_allowed_addr(controller_addr)) {
            printf("Failed to add controller MAC to Bluepad32 allowlist: %s\n", mac_addrs[i]);
            return 1;
        }
    }

    // Finally, enable the allowlist.
    // Similar to the "add_addr", its value gets stored in the NVS.
    uni_bt_allowlist_set_enabled(true);
    uni_bt_allowlist_list();

    if (chopper_runtime_start() != 0) {
        return 1;
    }

    // Does not return.
    btstack_run_loop_execute();
    return 0;
#endif
}
