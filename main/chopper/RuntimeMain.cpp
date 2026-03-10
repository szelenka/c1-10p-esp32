#include "chopper/Application.h"
#include "chopper/config/HardwareConfig.h"
#include "chopper/bluetooth/RoleManager.h"
#include "chopper/nodes/BodyUtilityNode.h"
#include "chopper/nodes/BluepadInputNode.h"
#include "chopper/nodes/DomeNode.h"
#include "chopper/nodes/DomeArmsNode.h"
#include "chopper/nodes/DriveNode.h"
#include "chopper/nodes/DriverUpdateNode.h"
#include "chopper/nodes/PeriscopeNode.h"
#include "chopper/nodes/SoundNode.h"
#include "chopper/hal/DriverManager.h"
#include "chopper/hal/ChopperBluetooth.h"
#include "chopper/hal/HardwareSerialPort.h"
#include "chopper/hal/MaestroServoDriver.h"
#include "chopper/hal/MP3AudioDriver.h"
#include "chopper/hal/SabertoothMotorDriver.h"
#include "chopper/hal/SoftwareSerialPort.h"
#include "chopper/hal/UartBusManager.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "driver/gpio.h"

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
    // Current runtime uses blocking serial writes in HAL drivers, so loop
    // execution can exceed 25 ms under normal command load.
    exec_cfg.max_loop_time_us = 100000;
    exec_cfg.loop_timeout_triggers_estop = false;
#if defined(CONFIG_FREERTOS_UNICORE) && CONFIG_FREERTOS_UNICORE
    exec_cfg.executor_task_core = 0;
#else
    exec_cfg.executor_task_core = 1;
#endif

    static chopper::Application app(exec_cfg);

    // Runtime HAL wiring (equivalent to legacy sketch setup functions).
    //
    // Notes:
    // - sw_serial requires an RX pin even for TX-only buses. We assign a
    //   dummy input pin for Sabertooth RX to satisfy that requirement.
    // - All HAL objects are static to preserve lifetime across executor tasks.
    static constexpr gpio_num_t kSabertoothDummyRxPin = GPIO_NUM_35;
    static chopper::hal::SoftwareSerialPort sabertooth_serial(
        static_cast<gpio_num_t>(chopper::config::pins::SABERTOOTH_TX),
        kSabertoothDummyRxPin,
        chopper::config::baud::SABERTOOTH);
    static chopper::hal::SoftwareSerialPort maestro_body_serial(
        static_cast<gpio_num_t>(chopper::config::pins::MAESTRO_BODY_TX),
        static_cast<gpio_num_t>(chopper::config::pins::MAESTRO_BODY_RX),
        chopper::config::baud::MAESTRO);
    static chopper::hal::SoftwareSerialPort maestro_dome_serial(
        static_cast<gpio_num_t>(chopper::config::pins::MAESTRO_DOME_TX),
        static_cast<gpio_num_t>(chopper::config::pins::MAESTRO_DOME_RX),
        chopper::config::baud::MAESTRO);
    static chopper::hal::SoftwareSerialPort mp3_serial(
        static_cast<gpio_num_t>(chopper::config::pins::MP3TRIGGER_TX),
        static_cast<gpio_num_t>(chopper::config::pins::MP3TRIGGER_RX),
        chopper::config::baud::MP3TRIGGER);
    static chopper::hal::HardwareSerialPort openmv_serial(
        UART_NUM_2,
        chopper::config::pins::OPENMV_TX,
        chopper::config::pins::OPENMV_RX,
        chopper::config::baud::OPENMV);

    static chopper::hal::SabertoothMotorDriver drive_left(
        sabertooth_serial,
        chopper::config::device_id::SABERTOOTH_TANK_DRIVE,
        1,
        "sabertooth_left");
    static chopper::hal::SabertoothMotorDriver drive_right(
        sabertooth_serial,
        chopper::config::device_id::SABERTOOTH_TANK_DRIVE,
        2,
        "sabertooth_right");
    static chopper::hal::SabertoothMotorDriver dome_motor(
        sabertooth_serial,
        chopper::config::device_id::SABERTOOTH_DOME_DRIVE,
        1,
        "sabertooth_dome");
    static chopper::hal::MaestroServoDriver maestro_body(
        maestro_body_serial,
        chopper::config::servo_channel::BODY_CHANNEL_COUNT,
        "maestro_body",
        chopper::config::device_id::MAESTRO_BODY);
    static chopper::hal::MaestroServoDriver maestro_dome(
        maestro_dome_serial,
        chopper::config::servo_channel::DOME_CHANNEL_COUNT,
        "maestro_dome",
        chopper::config::device_id::MAESTRO_DOME);
    static chopper::hal::MP3AudioDriver mp3(mp3_serial, "mp3");

    auto& uart_bus = chopper::hal::UartBusManager::getInstance();
    if (!uart_bus.acquirePort({
            0,
            static_cast<int8_t>(kSabertoothDummyRxPin),
            static_cast<int8_t>(chopper::config::pins::SABERTOOTH_TX),
            chopper::config::baud::SABERTOOTH,
            true,
            true,
            "sabertooth_bus"})) {
        ESP_LOGE(TAG, "Failed to acquire Sabertooth UART bus");
        return 1;
    }
    if (!uart_bus.acquirePort({
            1,
            static_cast<int8_t>(chopper::config::pins::MAESTRO_BODY_RX),
            static_cast<int8_t>(chopper::config::pins::MAESTRO_BODY_TX),
            chopper::config::baud::MAESTRO,
            true,
            false,
            "maestro_body_bus"})) {
        ESP_LOGE(TAG, "Failed to acquire body Maestro UART bus");
        return 1;
    }
    if (!uart_bus.acquirePort({
            2,
            static_cast<int8_t>(chopper::config::pins::MAESTRO_DOME_RX),
            static_cast<int8_t>(chopper::config::pins::MAESTRO_DOME_TX),
            chopper::config::baud::MAESTRO,
            true,
            false,
            "maestro_dome_bus"})) {
        ESP_LOGE(TAG, "Failed to acquire dome Maestro UART bus");
        return 1;
    }
    if (!uart_bus.acquirePort({
            3,
            static_cast<int8_t>(chopper::config::pins::MP3TRIGGER_RX),
            static_cast<int8_t>(chopper::config::pins::MP3TRIGGER_TX),
            chopper::config::baud::MP3TRIGGER,
            true,
            false,
            "mp3_bus"})) {
        ESP_LOGE(TAG, "Failed to acquire MP3 UART bus");
        return 1;
    }
    if (!uart_bus.acquirePort({
            4,
            static_cast<int8_t>(chopper::config::pins::OPENMV_RX),
            static_cast<int8_t>(chopper::config::pins::OPENMV_TX),
            chopper::config::baud::OPENMV,
            false,
            false,
            "openmv_uart2"})) {
        ESP_LOGE(TAG, "Failed to acquire OpenMV UART bus");
        return 1;
    }
    if (!uart_bus.validateAllocations()) {
        ESP_LOGE(TAG, "UART bus allocation validation failed");
        return 1;
    }
    uart_bus.printAllocations();

    sabertooth_serial.begin();
    maestro_body_serial.begin();
    maestro_dome_serial.begin();
    mp3_serial.begin();
    openmv_serial.begin();

    // Legacy setupLeds(): configure LED GPIOs and default to OFF.
    gpio_set_direction(static_cast<gpio_num_t>(chopper::config::pins::LED_FRONT), GPIO_MODE_OUTPUT);
    gpio_set_level(static_cast<gpio_num_t>(chopper::config::pins::LED_FRONT), 0);
    if (chopper::config::pins::LED_BACK != chopper::config::pins::LED_FRONT) {
        gpio_set_direction(static_cast<gpio_num_t>(chopper::config::pins::LED_BACK), GPIO_MODE_OUTPUT);
        gpio_set_level(static_cast<gpio_num_t>(chopper::config::pins::LED_BACK), 0);
    }

    // Runtime E2E path uses serial telemetry for local UI bridging.
    // Keep HTTP/WS disabled here unless network stack is explicitly initialized.
    chopper::telemetry::TelemetryService::Config telemetry_cfg{};
    telemetry_cfg.serial_enabled = true;
    telemetry_cfg.serial_compact = false;
    telemetry_cfg.async_enabled = true;
    telemetry_cfg.async_task_core = 0;
    telemetry_cfg.http_enabled = false;
    telemetry_cfg.websocket_enabled = false;
    app.configureTelemetry(telemetry_cfg);

    ESP_LOGI(TAG,
             "Executor cfg: hz=%u max_loop_us=%u core=%d loop_estop=%d node_estop=%d",
             exec_cfg.loop_frequency_hz,
             exec_cfg.max_loop_time_us,
             exec_cfg.executor_task_core,
             exec_cfg.loop_timeout_triggers_estop ? 1 : 0,
             exec_cfg.node_timeout_triggers_estop ? 1 : 0);
    ESP_LOGI(TAG, "Telemetry enabled (serial JSON only; HTTP/WS disabled in runtime)");

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

    auto drive_node = std::make_shared<chopper::nodes::DriveNode>();
    if (!app.addNode(drive_node)) {
        ESP_LOGE(TAG, "Failed to add DriveNode");
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

    auto sound_node = std::make_shared<chopper::nodes::SoundNode>();
    if (!app.addNode(sound_node)) {
        ESP_LOGE(TAG, "Failed to add SoundNode");
        return 1;
    }

    auto body_utility_node = std::make_shared<chopper::nodes::BodyUtilityNode>();
    if (!app.addNode(body_utility_node)) {
        ESP_LOGE(TAG, "Failed to add BodyUtilityNode");
        return 1;
    }

    auto dome_arms_node = std::make_shared<chopper::nodes::DomeArmsNode>();
    if (!app.addNode(dome_arms_node)) {
        ESP_LOGE(TAG, "Failed to add DomeArmsNode");
        return 1;
    }

    auto periscope_node = std::make_shared<chopper::nodes::PeriscopeNode>();
    if (!app.addNode(periscope_node)) {
        ESP_LOGE(TAG, "Failed to add PeriscopeNode");
        return 1;
    }

    auto driver_update_node = std::make_shared<chopper::nodes::DriverUpdateNode>(50.0);
    if (!app.addNode(driver_update_node)) {
        ESP_LOGE(TAG, "Failed to add DriverUpdateNode");
        return 1;
    }

    if (!app.addMotor(0, &drive_left, "drive/cmd")) {
        ESP_LOGE(TAG, "Failed to add left drive motor");
        return 1;
    }
    if (!app.addMotor(1, &drive_right, "drive/cmd")) {
        ESP_LOGE(TAG, "Failed to add right drive motor");
        return 1;
    }
    if (!app.addMotor(2, &dome_motor, "dome/motor/cmd")) {
        ESP_LOGE(TAG, "Failed to add dome motor");
        return 1;
    }
    if (!app.addServoController(&maestro_body, "servo/body/cmd")) {
        ESP_LOGE(TAG, "Failed to add body servo controller");
        return 1;
    }
    if (!app.addServoController(&maestro_dome, "servo/dome/cmd")) {
        ESP_LOGE(TAG, "Failed to add dome servo controller");
        return 1;
    }
    if (!app.addAudio(&mp3, "audio/cmd")) {
        ESP_LOGE(TAG, "Failed to add MP3 audio driver");
        return 1;
    }

    auto& driver_manager = chopper::hal::DriverManager::getInstance();
    if (!driver_manager.registerDriver(&drive_left, 30) ||
        !driver_manager.registerDriver(&drive_right, 30) ||
        !driver_manager.registerDriver(&dome_motor, 30) ||
        !driver_manager.registerDriver(&maestro_body, 40) ||
        !driver_manager.registerDriver(&maestro_dome, 41) ||
        !driver_manager.registerDriver(&mp3, 50)) {
        ESP_LOGE(TAG, "Failed to register one or more HAL drivers");
        return 1;
    }

    if (!app.init()) {
        ESP_LOGE(TAG, "Application init failed");
        return 1;
    }

    // Legacy setupMp3Trigger() set initial volume during setup.
    mp3.setVolume(20);

    if (!app.start()) {
        ESP_LOGE(TAG, "Application start failed");
        return 1;
    }

    g_runtime_started = true;
    ESP_LOGI(TAG, "Runtime started: Bluepad on BT task, Application executor on core %d",
             exec_cfg.executor_task_core);
    return 0;
}
