// SPDX-License-Identifier: Apache-2.0
// Copyright 2019 Ricardo Quesada
// http://retro.moe/unijoysticle2

#include "sdkconfig.h"

#include <stddef.h>

// BTstack related
#include <btstack_port_esp32.h>
#include <btstack_run_loop.h>
#include <btstack_stdio_esp32.h>

// Bluepad32 related
#include <arduino_platform.h>
#include <uni.h>

#include "SettingsBluetooth.h"

//
// Autostart
//
#if CONFIG_AUTOSTART_ARDUINO
void initBluepad32() {
#else
int app_main(void) {
#endif  // !CONFIG_AUTOSTART_ARDUINO
    // hci_dump_open(NULL, HCI_DUMP_STDOUT);

// Don't use BTstack buffered UART. It conflicts with the console.
#ifndef CONFIG_ESP_CONSOLE_UART_NONE
#ifndef CONFIG_BLUEPAD32_USB_CONSOLE_ENABLE
    btstack_stdio_init();
#endif  // CONFIG_BLUEPAD32_USB_CONSOLE_ENABLE
#endif  // CONFIG_ESP_CONSOLE_UART_NONE

    // Configure BTstack for ESP32 VHCI Controller
    btstack_init();

    // hci_dump_init(hci_dump_embedded_stdout_get_instance());

    // Must be called before uni_init()
    uni_platform_set_custom(get_arduino_platform());

    // Init Bluepad32.
    uni_init(0 /* argc */, NULL /* argv */);

    if (CONTROLLER_FORGET_MAC_ADDR)
    {
        uni_bt_del_keys_safe();
    }

    // Loop through the addresses and add them to the allowlist
    for (size_t i = 0; i < sizeof(CONTROLLER_MAC_ADDRS) / sizeof(CONTROLLER_MAC_ADDRS[0]); i++)
    {
        bd_addr_t controller_addr;
        // Parse human-readable Bluetooth address.
        sscanf_bd_addr(CONTROLLER_MAC_ADDRS[i], controller_addr);

        // Notice that this address will be added in the Non-volatile-storage (NVS).
        // If the device reboots, the address will still be stored.
        // Adding a duplicate value will do nothing.
        // You can add up to four entries in the allowlist.
        uni_bt_allowlist_add_addr(controller_addr);
    }
    
    // Finally, enable the allowlist.
    // Similar to the "add_addr", its value gets stored in the NVS.
    uni_bt_allowlist_set_enabled(true);

    // Does not return.
    btstack_run_loop_execute();
#if !CONFIG_AUTOSTART_ARDUINO
    return 0;
#endif  // !CONFIG_AUTOSTART_ARDUINO
}
