#include "chopper/hal/ChopperBluetooth.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"

static const char* const TAG = "ValidationMain";

namespace {
void validationTask(void*) {
    ESP_LOGI(TAG, "Starting BLUEPAD32-only validation runtime");

    bool last_connected[CHOPPER_BT_MAX_DEVICES] = {false};
    uint64_t last_report_seen[CHOPPER_BT_MAX_DEVICES] = {0};
    uint16_t last_buttons[CHOPPER_BT_MAX_DEVICES] = {0};
    uint8_t last_dpad[CHOPPER_BT_MAX_DEVICES] = {0};
    uint8_t last_misc[CHOPPER_BT_MAX_DEVICES] = {0};

    while (true) {
        int connected = chopper_bt_connected_count();
        ESP_LOGI(TAG, "connected_count=%d", connected);

        for (int i = 0; i < CHOPPER_BT_MAX_DEVICES; i++) {
            chopper_gamepad_data_t data{};
            if (chopper_bt_get_gamepad(i, &data) != 0) {
                continue;
            }

            if (data.connected != last_connected[i]) {
                ESP_LOGI(TAG, "slot=%d connected=%d mac=%02X:%02X:%02X:%02X:%02X:%02X type=%u batt=%u", i,
                         data.connected ? 1 : 0, data.btaddr[0], data.btaddr[1], data.btaddr[2], data.btaddr[3],
                         data.btaddr[4], data.btaddr[5], static_cast<unsigned>(data.controller_type),
                         static_cast<unsigned>(data.battery));
                last_connected[i] = data.connected;
                if (!data.connected) {
                    last_report_seen[i] = 0;
                    last_buttons[i] = 0;
                    last_dpad[i] = 0;
                    last_misc[i] = 0;
                }
            }

            if (data.connected && data.last_report_time_us != 0 && data.last_report_time_us != last_report_seen[i]) {
                last_report_seen[i] = data.last_report_time_us;
                const uint16_t buttons = data.gamepad.buttons;
                const uint8_t dpad = data.gamepad.dpad;
                const uint8_t misc = data.gamepad.misc_buttons;
                if (buttons != last_buttons[i] || dpad != last_dpad[i] || misc != last_misc[i]) {
                    ESP_LOGI(TAG, "slot=%d input btn=0x%04x dpad=0x%02x misc=0x%02x axes=(%ld,%ld,%ld,%ld)", i,
                             static_cast<unsigned>(buttons), static_cast<unsigned>(dpad), static_cast<unsigned>(misc),
                             static_cast<long>(data.gamepad.axis_x), static_cast<long>(data.gamepad.axis_y),
                             static_cast<long>(data.gamepad.axis_rx), static_cast<long>(data.gamepad.axis_ry));
                    last_buttons[i] = buttons;
                    last_dpad[i] = dpad;
                    last_misc[i] = misc;
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
}  // namespace

extern "C" int chopper_validation_main(void) {
    esp_err_t nvs_err = nvs_flash_init();
    if (nvs_err == ESP_ERR_NVS_NO_FREE_PAGES || nvs_err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    } else if (nvs_err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_flash_init failed: %d", static_cast<int>(nvs_err));
    }

    BaseType_t ok = xTaskCreate(validationTask, "bluepad_validate", 8192, nullptr, tskIDLE_PRIORITY + 2, nullptr);

    if (ok != pdPASS) {
        ESP_LOGE(TAG, "Failed to create validation task");
        return 1;
    }

    return 0;
}
