#include "sdkconfig.h"

#ifdef CHOPPER_VALIDATION_RUNTIME

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "Bluepad32.h"
#include "btstack_run_loop.h"
#include <btstack.h>
#include "uni_log.h"

// bluepad32_arduino expects Arduino sketch entry points when
// CONFIG_AUTOSTART_ARDUINO=n. Validation mode does not use a sketch, so
// provide minimal stubs that start scanning and pump BP32 updates.
static volatile int g_connected_controllers = 0;
static ControllerPtr g_controllers[BP32_MAX_GAMEPADS] = {nullptr};
static uint32_t g_last_heartbeat_ms = 0;
static btstack_context_callback_registration_t g_link_policy_cb;

static void apply_link_policy_on_bt_thread(void* context) {
    ARG_UNUSED(context);
    // Validation mode must mirror runtime link policy:
    // Disable BR/EDR Sniff mode to prioritize low-latency, stable controller input.
    //
    // Why: Sniff is power-saving, but it introduces scheduled sleep/wake windows.
    // With two active controllers this can create timing jitter and "stale input"
    // behavior. For robot control validation we want deterministic input timing.
    //
    // Keep ROLE_SWITCH for compatibility; omit SNIFF to force active links.
    // This function is executed on BTstack thread via execute_on_main_thread(),
    // so calling BTstack API here is thread-safe.
    gap_set_default_link_policy_settings(LM_LINK_POLICY_ENABLE_ROLE_SWITCH);
    logi("VAL: BT link policy applied (sniff disabled, role switch enabled)\n");
}

static void onConnectedController(ControllerPtr ctl) {
    if (ctl && ctl->index() >= 0 && ctl->index() < BP32_MAX_GAMEPADS) {
        g_controllers[ctl->index()] = ctl;
        logi("VAL: connected idx=%d model=%s\n", ctl->index(), ctl->getModelName().c_str());
    }
    g_connected_controllers++;
}

static void onDisconnectedController(ControllerPtr ctl) {
    if (ctl && ctl->index() >= 0 && ctl->index() < BP32_MAX_GAMEPADS) {
        g_controllers[ctl->index()] = nullptr;
        logi("VAL: disconnected idx=%d\n", ctl->index());
    }
    if (g_connected_controllers > 0) {
        g_connected_controllers--;
    }
}

void setup() {
    BP32.setup(&onConnectedController, &onDisconnectedController, true);
    g_link_policy_cb.callback = &apply_link_policy_on_bt_thread;
    g_link_policy_cb.context = nullptr;
    btstack_run_loop_execute_on_main_thread(&g_link_policy_cb);
}

void loop() {
    BP32.update();

    const uint32_t now_ms = millis();
    if ((now_ms - g_last_heartbeat_ms) >= 1000) {
        g_last_heartbeat_ms = now_ms;
        logi("VAL: heartbeat connected=%d\n", g_connected_controllers);
        for (int i = 0; i < BP32_MAX_GAMEPADS; ++i) {
            ControllerPtr c = g_controllers[i];
            if (!c) {
                continue;
            }
            logi("VAL: idx=%d isConnected=%d hasData=%d buttons=0x%04x dpad=0x%02x axes=(%ld,%ld,%ld,%ld)\n",
                 i,
                 c->isConnected() ? 1 : 0,
                 c->hasData() ? 1 : 0,
                 static_cast<unsigned>(c->buttons()),
                 static_cast<unsigned>(c->dpad()),
                 static_cast<long>(c->axisX()),
                 static_cast<long>(c->axisY()),
                 static_cast<long>(c->axisRX()),
                 static_cast<long>(c->axisRY()));
        }
    }

    vTaskDelay(pdMS_TO_TICKS(10));
}

#endif  // CHOPPER_VALIDATION_RUNTIME
