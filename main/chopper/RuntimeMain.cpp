#include "chopper/Application.h"
#include "chopper/config/HardwareConfig.h"
#include "chopper/bluetooth/RoleManager.h"
#include "chopper/nodes/BluepadInputNode.h"
#include "chopper/nodes/DomeNode.h"
#include "chopper/hal/ChopperBluetooth.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"

#include <cstdio>

namespace {
static const char* TAG = "RuntimeMain";
static chopper::bluetooth::MacBasedPolicy g_role_policy;
static bool g_policy_initialized = false;
static bool g_runtime_started = false;

bool parseMacAddress(const char* text, chopper::bluetooth::MacAddress& out) {
    unsigned int b[6] = {0};
    if (!text) {
        return false;
    }
    const int n = std::sscanf(text, "%02x:%02x:%02x:%02x:%02x:%02x",
                              &b[0], &b[1], &b[2], &b[3], &b[4], &b[5]);
    if (n != 6) {
        return false;
    }
    for (int i = 0; i < 6; ++i) {
        out.addr[i] = static_cast<uint8_t>(b[i] & 0xFF);
    }
    return true;
}

void initRolePolicy() {
    if (g_policy_initialized) {
        return;
    }

    const struct {
        const char* mac;
        chopper::bluetooth::ControllerRole role;
    } mappings[] = {
        {chopper::config::bluetooth::DRIVE_MAC, chopper::bluetooth::ControllerRole::DRIVE},
        {chopper::config::bluetooth::DOME_MAC, chopper::bluetooth::ControllerRole::DOME},
        {chopper::config::bluetooth::ANIMATE_MAC, chopper::bluetooth::ControllerRole::ANIMATION},
        {chopper::config::bluetooth::CAMERA_MAC, chopper::bluetooth::ControllerRole::CAMERA},
    };

    for (const auto& mapping : mappings) {
        chopper::bluetooth::MacAddress mac{};
        if (parseMacAddress(mapping.mac, mac)) {
            g_role_policy.addMapping(mac, mapping.role);
        } else {
            ESP_LOGW(TAG, "Invalid configured MAC: %s", mapping.mac ? mapping.mac : "(null)");
        }
    }

    g_policy_initialized = true;
}

void onUnexpectedControllerDisconnect(uint8_t slot_index,
                                      chopper::bluetooth::ControllerRole role,
                                      uint64_t stale_ms,
                                      void* context) {
    auto* app = static_cast<chopper::Application*>(context);
    ESP_LOGE(TAG, "Unexpected controller loss: slot=%u role=%s stale=%llu ms",
             static_cast<unsigned>(slot_index),
             chopper::bluetooth::roleToString(role),
             static_cast<unsigned long long>(stale_ms));
    if (app) {
        app->softStop("Unexpected controller disconnect");
    }
}

void onControllerConnected(uint8_t slot_index,
                           chopper::bluetooth::ControllerRole role,
                           void* context) {
    auto* app = static_cast<chopper::Application*>(context);
    if (app) {
        auto& cm = app->getControllerManager();
        const auto& slot = cm.getSlot(slot_index);

        const struct {
            const char* mac;
            chopper::bluetooth::ControllerRole role;
        } mappings[] = {
            {chopper::config::bluetooth::DRIVE_MAC, chopper::bluetooth::ControllerRole::DRIVE},
            {chopper::config::bluetooth::DOME_MAC, chopper::bluetooth::ControllerRole::DOME},
            {chopper::config::bluetooth::ANIMATE_MAC, chopper::bluetooth::ControllerRole::ANIMATION},
            {chopper::config::bluetooth::CAMERA_MAC, chopper::bluetooth::ControllerRole::CAMERA},
        };

        chopper::bluetooth::ControllerRole expected = chopper::bluetooth::ControllerRole::UNASSIGNED;
        for (const auto& m : mappings) {
            chopper::bluetooth::MacAddress cfg{};
            if (parseMacAddress(m.mac, cfg) && cfg == slot.mac) {
                expected = m.role;
                break;
            }
        }

        if (expected != chopper::bluetooth::ControllerRole::UNASSIGNED &&
            role != expected) {
            if (cm.getRoleManager().assignRole(slot_index, expected)) {
                role = expected;
                ESP_LOGW(TAG, "Role corrected by MAC mapping: slot=%u role=%s",
                             static_cast<unsigned>(slot_index),
                             chopper::bluetooth::roleToString(role));
            }
        }
    }

    ESP_LOGI(TAG, "Controller connected: slot=%u role=%s -> clear soft stop",
             static_cast<unsigned>(slot_index),
             chopper::bluetooth::roleToString(role));
    if (app) {
        app->clearSoftStop("Controller connected");
    }
}
} // namespace

extern "C" int chopper_runtime_start(void) {
    if (g_runtime_started) {
        return 0;
    }

    chopper::core::Executor::Config exec_cfg{};
    exec_cfg.loop_frequency_hz = 50;
    // 50 Hz loop => 20 ms period. Allow startup/telemetry bursts without
    // immediate E-stop while still detecting sustained stalls.
    exec_cfg.max_loop_time_us = 25000;
#if defined(CONFIG_FREERTOS_UNICORE) && CONFIG_FREERTOS_UNICORE
    exec_cfg.executor_task_core = 0;
#else
    exec_cfg.executor_task_core = 1;
#endif

    static chopper::Application app(exec_cfg);

    // Runtime currently does not initialize esp_netif/tcpip stack before app init.
    // Keep telemetry local (serial/async) and disable HTTP/WS endpoints to avoid
    // LWIP "Invalid mbox" asserts from httpd_start().
    chopper::telemetry::TelemetryService::Config telem_cfg{};
    telem_cfg.async_enabled = true;
    telem_cfg.http_enabled = false;
    telem_cfg.websocket_enabled = false;
    app.configureTelemetry(telem_cfg);

    ESP_LOGI(TAG,
             "Executor cfg: hz=%u max_loop_us=%u core=%d loop_estop=%d node_estop=%d",
             exec_cfg.loop_frequency_hz,
             exec_cfg.max_loop_time_us,
             exec_cfg.executor_task_core,
             exec_cfg.loop_timeout_triggers_estop ? 1 : 0,
             exec_cfg.node_timeout_triggers_estop ? 1 : 0);
    ESP_LOGI(TAG,
             "Telemetry cfg: async=%d http=%d ws=%d serial=%d update_hz=%.1f",
             telem_cfg.async_enabled ? 1 : 0,
             telem_cfg.http_enabled ? 1 : 0,
             telem_cfg.websocket_enabled ? 1 : 0,
             telem_cfg.serial_enabled ? 1 : 0,
             telem_cfg.node_update_hz);

    initRolePolicy();
    app.getControllerManager().getRoleManager().setPolicy(g_role_policy.getPolicy());
    app.getControllerManager().setConnectCallback(&onControllerConnected, &app);
    app.getControllerManager().setUnexpectedDisconnectCallback(
        &onUnexpectedControllerDisconnect, &app);

    auto input_node =
        std::make_shared<chopper::nodes::BluepadInputNode>(&app.getControllerManager(), false, 50.0);
    if (!app.addNode(input_node)) {
        ESP_LOGE(TAG, "Failed to add BluepadInputNode");
        return 1;
    }

    // DOME controller drives a single dome motor on its own command topic.
    // Use motor_id=2 so it remains distinct from DRIVE motor IDs 0/1.
    auto dome_node = std::make_shared<chopper::nodes::DomeNode>(
        nullptr, 0.5f, 2.0f, 2, false);
    if (!app.addNode(dome_node)) {
        ESP_LOGE(TAG, "Failed to add DomeNode");
        return 1;
    }

    if (!app.init()) {
        ESP_LOGE(TAG, "Application init failed");
        return 1;
    }
    if (!app.start()) {
        ESP_LOGE(TAG, "Application start failed");
        return 1;
    }

    g_runtime_started = true;
    ESP_LOGI(TAG, "Runtime started: Bluepad on BT task, Application executor on core %d",
             exec_cfg.executor_task_core);
    return 0;
}
