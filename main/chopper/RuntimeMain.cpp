#include "chopper/Application.h"
#include "chopper/config/DefaultParameters.h"
#include "chopper/config/HardwareConfig.h"
#include "chopper/core/ParameterServer.h"
#include "chopper/bluetooth/RoleManager.h"
#include "chopper/nodes/BodyDoorsNode.h"
#include "chopper/nodes/BodyLedNode.h"
#include "chopper/nodes/BodyUtilityNode.h"
#include "chopper/nodes/BluepadInputNode.h"
#include "chopper/dome/DomePosition.h"
#include "chopper/dome/RSSMechanism.h"
#include "chopper/nodes/DomeNode.h"
#include "chopper/nodes/DomeArmsNode.h"
#include "chopper/nodes/DriveNode.h"
#include "chopper/nodes/DriverUpdateNode.h"
#include "chopper/nodes/NeckNode.h"
#include "chopper/nodes/OpenMvBridgeNode.h"
#include "chopper/nodes/PeriscopeNode.h"
#include "chopper/nodes/ServoMotionNode.h"
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
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/ledc.h"

namespace {
static const char* const TAG = "RuntimeMain";
struct ConfiguredRoleMapping {
    chopper::bluetooth::MacAddress mac{};
    chopper::bluetooth::ControllerRole role = chopper::bluetooth::ControllerRole::UNASSIGNED;
};

constexpr uint8_t kConfiguredRoleMappingMax = 5;
// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables)
static chopper::bluetooth::MacBasedPolicy g_role_policy;  // NOLINT(bugprone-throwing-static-initialization)
static bool g_policy_initialized = false;
static bool g_runtime_started = false;
static ConfiguredRoleMapping g_configured_role_mappings[kConfiguredRoleMappingMax] = {};
static uint8_t g_configured_role_mapping_count = 0;
static chopper::bluetooth::ControllerRole g_soft_stop_required_role = chopper::bluetooth::ControllerRole::UNASSIGNED;
// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)

constexpr ledc_timer_t LED_TIMER = LEDC_TIMER_0;
constexpr ledc_channel_t LED_CHANNEL = LEDC_CHANNEL_0;
constexpr int LED_DUTY_RESOLUTION = LEDC_TIMER_8_BIT;  // 0–255

void ledWriteCallback(uint8_t brightness, void* /*context*/) {
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LED_CHANNEL, brightness);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LED_CHANNEL);
}

int8_t hexNibble(char c) {
    if (c >= '0' && c <= '9') {
        return static_cast<int8_t>(c - '0');
    }
    if (c >= 'A' && c <= 'F') {
        return static_cast<int8_t>((c - 'A') + 10);
    }
    if (c >= 'a' && c <= 'f') {
        return static_cast<int8_t>((c - 'a') + 10);
    }
    return -1;
}

[[nodiscard]] bool parseMacAddress(const char* text, chopper::bluetooth::MacAddress& out) {
    if (text == nullptr) {
        return false;
    }
    chopper::bluetooth::MacAddress parsed{};
    for (int i = 0; i < 6; ++i) {
        const int8_t high = hexNibble(text[0]);
        const int8_t low = hexNibble(text[1]);
        if (high < 0 || low < 0) {
            return false;
        }
        parsed.addr[i] = static_cast<uint8_t>((static_cast<uint8_t>(high) << 4U) | static_cast<uint8_t>(low));
        text += 2;
        if (i < 5) {
            if (*text != ':') {
                return false;
            }
            ++text;
        }
    }
    if (*text != '\0') {
        return false;
    }
    out = parsed;
    return true;
}

bool isActuatorControllerRole(chopper::bluetooth::ControllerRole role) {
    return role == chopper::bluetooth::ControllerRole::DRIVE || role == chopper::bluetooth::ControllerRole::DOME;
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
        {chopper::config::bluetooth::TEMBED_MAC, chopper::bluetooth::ControllerRole::DRIVE},
    };

    for (const auto& mapping : mappings) {
        chopper::bluetooth::MacAddress mac{};
        if (parseMacAddress(mapping.mac, mac)) {
            if (g_configured_role_mapping_count >= kConfiguredRoleMappingMax) {
                ESP_LOGW(TAG, "Role mapping table full, ignoring configured MAC: %s",
                         mapping.mac ? mapping.mac : "(null)");
            } else if (g_role_policy.addMapping(mac, mapping.role)) {
                g_configured_role_mappings[g_configured_role_mapping_count] = ConfiguredRoleMapping{mac, mapping.role};
                ++g_configured_role_mapping_count;
            } else {
                ESP_LOGW(TAG, "Role policy table full, ignoring configured MAC: %s",
                         mapping.mac ? mapping.mac : "(null)");
            }
        } else {
            ESP_LOGW(TAG, "Invalid configured MAC: %s", mapping.mac ? mapping.mac : "(null)");
        }
    }

    g_policy_initialized = true;
}

void onUnexpectedControllerDisconnect(uint8_t slot_index, chopper::bluetooth::ControllerRole role, uint64_t stale_ms,
                                      void* context) {
    auto* app = static_cast<chopper::Application*>(context);
    ESP_LOGE(TAG, "Unexpected controller loss: slot=%u role=%s stale=%llu ms", static_cast<unsigned>(slot_index),
             chopper::bluetooth::roleToString(role), static_cast<unsigned long long>(stale_ms));
    if (app != nullptr) {
        if (isActuatorControllerRole(role)) {
            g_soft_stop_required_role = role;
        }
        app->softStop("Unexpected controller disconnect");
    }
}

void onControllerConnected(uint8_t slot_index, chopper::bluetooth::ControllerRole role, void* context) {
    auto* app = static_cast<chopper::Application*>(context);
    if (app != nullptr) {
        auto& cm = app->getControllerManager();
        const auto& slot = cm.getSlot(slot_index);

        chopper::bluetooth::ControllerRole expected = chopper::bluetooth::ControllerRole::UNASSIGNED;
        for (uint8_t i = 0; i < g_configured_role_mapping_count; ++i) {
            if (g_configured_role_mappings[i].mac == slot.mac) {
                expected = g_configured_role_mappings[i].role;
                break;
            }
        }

        if (expected != chopper::bluetooth::ControllerRole::UNASSIGNED && role != expected) {
            if (cm.getRoleManager().assignRole(slot_index, expected)) {
                role = expected;
                ESP_LOGW(TAG, "Role corrected by MAC mapping: slot=%u role=%s", static_cast<unsigned>(slot_index),
                         chopper::bluetooth::roleToString(role));
            }
        }
    }

    ESP_LOGI(TAG, "Controller connected: slot=%u role=%s", static_cast<unsigned>(slot_index),
             chopper::bluetooth::roleToString(role));
    if (app != nullptr) {
        if (app->getExecutor().isEmergencyStop()) {
            ESP_LOGW(TAG, "Controller connected while E-stop is latched; manual reset required");
            return;
        }

        if (!isActuatorControllerRole(role)) {
            ESP_LOGI(TAG, "Non-actuator controller connected; safety stop remains active");
            return;
        }

        if (app->getExecutor().isSoftStopped() &&
            g_soft_stop_required_role != chopper::bluetooth::ControllerRole::UNASSIGNED &&
            role != g_soft_stop_required_role) {
            ESP_LOGW(TAG, "Controller role %s connected, waiting for lost role %s before clearing soft stop",
                     chopper::bluetooth::roleToString(role),
                     chopper::bluetooth::roleToString(g_soft_stop_required_role));
            return;
        }

        app->clearSoftStop("Controller connected");
        g_soft_stop_required_role = chopper::bluetooth::ControllerRole::UNASSIGNED;
    }
}
}  // namespace

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
    // BT controller connect/disconnect can still cause short one-time spikes
    // from stack transitions and serial logging. Warn, but do not kill the
    // system for a transient connection event.
    exec_cfg.node_timeout_triggers_estop = false;
#if defined(CONFIG_FREERTOS_UNICORE) && CONFIG_FREERTOS_UNICORE
    exec_cfg.executor_task_core = 0;
#else
    exec_cfg.executor_task_core = 1;
#endif

    static chopper::Application app(exec_cfg);

    // Runtime HAL wiring (equivalent to legacy sketch setup functions).
    //
    // Notes:
    // - Sabertooth is TX-only from the ESP32 side, matching legacy NOT_A_PIN RX.
    // - All HAL objects are static to preserve lifetime across executor tasks.
    static chopper::hal::SoftwareSerialPort sabertooth_serial(
        static_cast<gpio_num_t>(chopper::config::pins::SABERTOOTH_TX), chopper::config::baud::SABERTOOTH);
    static chopper::hal::SoftwareSerialPort maestro_body_serial(
        static_cast<gpio_num_t>(chopper::config::pins::MAESTRO_BODY_TX),
        static_cast<gpio_num_t>(chopper::config::pins::MAESTRO_BODY_RX), chopper::config::baud::MAESTRO);
    static chopper::hal::SoftwareSerialPort maestro_dome_serial(
        static_cast<gpio_num_t>(chopper::config::pins::MAESTRO_DOME_TX),
        static_cast<gpio_num_t>(chopper::config::pins::MAESTRO_DOME_RX), chopper::config::baud::MAESTRO);
    static chopper::hal::SoftwareSerialPort mp3_serial(static_cast<gpio_num_t>(chopper::config::pins::MP3TRIGGER_TX),
                                                       static_cast<gpio_num_t>(chopper::config::pins::MP3TRIGGER_RX),
                                                       chopper::config::baud::MP3TRIGGER);
    static chopper::hal::HardwareSerialPort openmv_serial(
        UART_NUM_2, chopper::config::pins::OPENMV_TX, chopper::config::pins::OPENMV_RX, chopper::config::baud::OPENMV);

    static chopper::hal::SabertoothMotorDriver drive_left(
        sabertooth_serial, chopper::config::device_id::SABERTOOTH_TANK_DRIVE, 1, "sabertooth_left");
    static chopper::hal::SabertoothMotorDriver drive_right(
        sabertooth_serial, chopper::config::device_id::SABERTOOTH_TANK_DRIVE, 2, "sabertooth_right");
    static chopper::hal::SabertoothMotorDriver dome_motor(
        sabertooth_serial, chopper::config::device_id::SABERTOOTH_DOME_DRIVE, 1, "sabertooth_dome");
    static chopper::hal::MaestroServoDriver maestro_body(maestro_body_serial,
                                                         chopper::config::servo_channel::BODY_CHANNEL_COUNT,
                                                         "maestro_body", chopper::config::device_id::MAESTRO_BODY);
    static chopper::hal::MaestroServoDriver maestro_dome(maestro_dome_serial,
                                                         chopper::config::servo_channel::DOME_CHANNEL_COUNT,
                                                         "maestro_dome", chopper::config::device_id::MAESTRO_DOME);
    static chopper::hal::MP3AudioDriver mp3(mp3_serial, "mp3");

    auto& uart_bus = chopper::hal::UartBusManager::getInstance();
    if (!uart_bus.acquirePort({0, static_cast<int8_t>(chopper::config::pins::SABERTOOTH_RX),
                               static_cast<int8_t>(chopper::config::pins::SABERTOOTH_TX),
                               chopper::config::baud::SABERTOOTH, true, true, "sabertooth_bus"})) {
        ESP_LOGE(TAG, "Failed to acquire Sabertooth UART bus");
        return 1;
    }
    if (!uart_bus.acquirePort({1, static_cast<int8_t>(chopper::config::pins::MAESTRO_BODY_RX),
                               static_cast<int8_t>(chopper::config::pins::MAESTRO_BODY_TX),
                               chopper::config::baud::MAESTRO, true, false, "maestro_body_bus"})) {
        ESP_LOGE(TAG, "Failed to acquire body Maestro UART bus");
        return 1;
    }
    if (!uart_bus.acquirePort({2, static_cast<int8_t>(chopper::config::pins::MAESTRO_DOME_RX),
                               static_cast<int8_t>(chopper::config::pins::MAESTRO_DOME_TX),
                               chopper::config::baud::MAESTRO, true, false, "maestro_dome_bus"})) {
        ESP_LOGE(TAG, "Failed to acquire dome Maestro UART bus");
        return 1;
    }
    if (!uart_bus.acquirePort({3, static_cast<int8_t>(chopper::config::pins::MP3TRIGGER_RX),
                               static_cast<int8_t>(chopper::config::pins::MP3TRIGGER_TX),
                               chopper::config::baud::MP3TRIGGER, true, false, "mp3_bus"})) {
        ESP_LOGE(TAG, "Failed to acquire MP3 UART bus");
        return 1;
    }
    if (!uart_bus.acquirePort({4, static_cast<int8_t>(chopper::config::pins::OPENMV_RX),
                               static_cast<int8_t>(chopper::config::pins::OPENMV_TX), chopper::config::baud::OPENMV,
                               false, false, "openmv_uart2"})) {
        ESP_LOGE(TAG, "Failed to acquire OpenMV UART bus");
        return 1;
    }
    if (!uart_bus.validateAllocations()) {
        ESP_LOGE(TAG, "UART bus allocation validation failed");
        return 1;
    }
    uart_bus.printAllocations();

    if (!sabertooth_serial.begin()) {
        ESP_LOGE(TAG, "Failed to start Sabertooth software serial");
        return 1;
    }
    if (!maestro_body_serial.begin()) {
        ESP_LOGE(TAG, "Failed to start body Maestro software serial");
        return 1;
    }
    if (!maestro_dome_serial.begin()) {
        ESP_LOGE(TAG, "Failed to start dome Maestro software serial");
        return 1;
    }
    if (!mp3_serial.begin()) {
        ESP_LOGE(TAG, "Failed to start MP3 software serial");
        return 1;
    }
    // The SyRen on the shared packet-serial bus requires a two-second delay
    // before the 0xAA autobaud byte. Sabertooth 2x32 baud is configured in
    // DEScribe and must match config::baud::SABERTOOTH. No motor-controller
    // addressed packets are sent until DriverManager::initAll() below.
    constexpr uint32_t actuator_uart_ready_delay_ms = chopper::config::motor_controller::SYREN_AUTOBAUD_READY_DELAY_MS;
    constexpr uint32_t actuator_uart_settle_delay_ms =
        chopper::config::motor_controller::SYREN_AUTOBAUD_SETTLE_DELAY_MS;
    constexpr uint32_t audio_ready_delay_ms = chopper::config::sound::MP3TRIGGER_READY_DELAY_MS;
    constexpr uint32_t startup_serial_ready_delay_ms =
        (actuator_uart_ready_delay_ms > audio_ready_delay_ms) ? actuator_uart_ready_delay_ms : audio_ready_delay_ms;
    vTaskDelay(pdMS_TO_TICKS(startup_serial_ready_delay_ms));
    if (!chopper::hal::SabertoothMotorDriver::sendSharedAutobaud(sabertooth_serial)) {
        ESP_LOGE(TAG, "Failed to send Sabertooth/SyRen shared-bus autobaud");
        return 1;
    }
    vTaskDelay(pdMS_TO_TICKS(actuator_uart_settle_delay_ms));
    mp3.setVolume(chopper::config::sound::DEFAULT_VOLUME);
    openmv_serial.begin();

    // Configure LEDC PWM for body LED fading (replaces legacy analogWrite).
    ledc_timer_config_t led_timer_cfg{};
    led_timer_cfg.speed_mode = LEDC_LOW_SPEED_MODE;
    led_timer_cfg.timer_num = LED_TIMER;
    led_timer_cfg.duty_resolution = static_cast<ledc_timer_bit_t>(LED_DUTY_RESOLUTION);
    led_timer_cfg.freq_hz = 5000;
    led_timer_cfg.clk_cfg = LEDC_AUTO_CLK;
    ledc_timer_config(&led_timer_cfg);

    ledc_channel_config_t led_ch_cfg{};
    led_ch_cfg.speed_mode = LEDC_LOW_SPEED_MODE;
    led_ch_cfg.channel = LED_CHANNEL;
    led_ch_cfg.timer_sel = LED_TIMER;
    led_ch_cfg.gpio_num = chopper::config::pins::LED_FRONT;
    led_ch_cfg.duty = 0;
    led_ch_cfg.hpoint = 0;
    ledc_channel_config(&led_ch_cfg);

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

    ESP_LOGI(TAG, "Executor cfg: hz=%u max_loop_us=%u core=%d loop_estop=%d node_estop=%d", exec_cfg.loop_frequency_hz,
             exec_cfg.max_loop_time_us, exec_cfg.executor_task_core, exec_cfg.loop_timeout_triggers_estop ? 1 : 0,
             exec_cfg.node_timeout_triggers_estop ? 1 : 0);
    ESP_LOGI(TAG, "Telemetry enabled (serial JSON only; HTTP/WS disabled in runtime)");

    // Register all default parameters (servo limits, drive config, etc.)
    // before nodes are initialized so they read correct hardware values.
    chopper::config::registerDefaultParameters();

    auto& params = chopper::core::ParameterServer::getInstance();
    bool drive_left_inverted = true;
    bool drive_right_inverted = false;
    bool dome_inverted = false;
    (void)params.get("drive.motor1_inverted", drive_left_inverted);
    (void)params.get("drive.motor2_inverted", drive_right_inverted);
    (void)params.get("dome.motor_inverted", dome_inverted);
    drive_left.setInverted(drive_left_inverted);
    drive_right.setInverted(drive_right_inverted);
    dome_motor.setInverted(dome_inverted);

    float rss_base_altitude = 149.053f;
    float rss_effector_altitude = 193.350f;
    float rss_bottom_link = 45.0f;
    float rss_top_link = 31.0f;
    float rss_min_height = 28.621f;
    float rss_limit_normal = 0.25f;
    bool rss_bend_out = true;
    int32_t rss_actuation_range = 270;
    float rss_rotation_offset = -30.0f;
    int32_t neck_a_min = 2032;
    int32_t neck_b_min = 1952;
    int32_t neck_c_min = 2048;
    int32_t neck_a_max = 2256;
    int32_t neck_b_max = 2176;
    int32_t neck_c_max = 2272;
    (void)params.get("rss.base_altitude", rss_base_altitude);
    (void)params.get("rss.effector_altitude", rss_effector_altitude);
    (void)params.get("rss.bottom_link", rss_bottom_link);
    (void)params.get("rss.top_link", rss_top_link);
    (void)params.get("rss.min_height", rss_min_height);
    (void)params.get("rss.limit_normal", rss_limit_normal);
    (void)params.get("rss.bend_out", rss_bend_out);
    (void)params.get("rss.actuation_range", rss_actuation_range);
    (void)params.get("rss.rotation_offset", rss_rotation_offset);
    (void)params.get("servo.neck_a.min", neck_a_min);
    (void)params.get("servo.neck_b.min", neck_b_min);
    (void)params.get("servo.neck_c.min", neck_c_min);
    (void)params.get("servo.neck_a.max", neck_a_max);
    (void)params.get("servo.neck_b.max", neck_b_max);
    (void)params.get("servo.neck_c.max", neck_c_max);
    static chopper::dome::RSSMechanism rss_machine(rss_base_altitude, rss_effector_altitude, rss_bottom_link,
                                                   rss_top_link, rss_min_height, rss_limit_normal, rss_bend_out);
    rss_machine.setRotationAngleOffset(rss_rotation_offset);
    rss_machine.setActuationRange(static_cast<uint16_t>(rss_actuation_range));
    rss_machine.setLegMinPulse(static_cast<uint16_t>(neck_a_min), static_cast<uint16_t>(neck_b_min),
                               static_cast<uint16_t>(neck_c_min));
    rss_machine.setLegMaxPulse(static_cast<uint16_t>(neck_a_max), static_cast<uint16_t>(neck_b_max),
                               static_cast<uint16_t>(neck_c_max));

    initRolePolicy();
    app.getControllerManager().getRoleManager().setPolicy(g_role_policy.getPolicy());
    app.getControllerManager().setConnectCallback(&onControllerConnected, &app);
    app.getControllerManager().setUnexpectedDisconnectCallback(&onUnexpectedControllerDisconnect, &app);

    auto input_node = std::make_shared<chopper::nodes::BluepadInputNode>(&app.getControllerManager(), false, 50.0);
    if (!app.addNode(input_node)) {
        ESP_LOGE(TAG, "Failed to add BluepadInputNode");
        return 1;
    }

    auto drive_node = std::make_shared<chopper::nodes::DriveNode>();
    if (!app.addNode(drive_node)) {
        ESP_LOGE(TAG, "Failed to add DriveNode");
        return 1;
    }

    // Dome position tracker — seeded at home (0°) so auto-dome can run
    // even without a physical encoder.  If an encoder is wired later,
    // feed it via dome_position.update(angle, now_ms).
    static chopper::dome::DomePosition dome_position;
    dome_position.update(0, 0);

    // DOME controller drives a single dome motor on its own command topic.
    // Use motor_id=2 so it remains distinct from DRIVE motor IDs 0/1.
    auto dome_node = std::make_shared<chopper::nodes::DomeNode>(&dome_position, 0.5f, 2.0f, 2, false, 320);
    if (!app.addNode(dome_node)) {
        ESP_LOGE(TAG, "Failed to add DomeNode");
        return 1;
    }

    auto sound_node = std::make_shared<chopper::nodes::SoundNode>();
    if (!app.addNode(sound_node)) {
        ESP_LOGE(TAG, "Failed to add SoundNode");
        return 1;
    }

    auto body_servo_motion_node = std::make_shared<chopper::nodes::ServoMotionNode>(
        "body_servo_motion", "servo/body/move", "servo/body/cmd", chopper::config::servo_channel::BODY_CHANNEL_COUNT);
    if (!app.addNode(body_servo_motion_node)) {
        ESP_LOGE(TAG, "Failed to add body ServoMotionNode");
        return 1;
    }

    auto body_utility_node = std::make_shared<chopper::nodes::BodyUtilityNode>();
    if (!app.addNode(body_utility_node)) {
        ESP_LOGE(TAG, "Failed to add BodyUtilityNode");
        return 1;
    }

    auto body_doors_node = std::make_shared<chopper::nodes::BodyDoorsNode>();
    if (!app.addNode(body_doors_node)) {
        ESP_LOGE(TAG, "Failed to add BodyDoorsNode");
        return 1;
    }

    auto body_led_node = std::make_shared<chopper::nodes::BodyLedNode>(&ledWriteCallback);
    if (!app.addNode(body_led_node)) {
        ESP_LOGE(TAG, "Failed to add BodyLedNode");
        return 1;
    }

    auto dome_servo_motion_node = std::make_shared<chopper::nodes::ServoMotionNode>(
        "dome_servo_motion", "servo/dome/move", "servo/dome/cmd", chopper::config::servo_channel::DOME_CHANNEL_COUNT);
    if (!app.addNode(dome_servo_motion_node)) {
        ESP_LOGE(TAG, "Failed to add dome ServoMotionNode");
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

    auto neck_node = std::make_shared<chopper::nodes::NeckNode>(
        &rss_machine, "controller/dome", "servo/body/cmd", chopper::config::servo_channel::BODY_NECK_A,
        chopper::config::servo_channel::BODY_NECK_B, chopper::config::servo_channel::BODY_NECK_C);
    if (!app.addNode(neck_node)) {
        ESP_LOGE(TAG, "Failed to add NeckNode");
        return 1;
    }

    auto openmv_node = std::make_shared<chopper::nodes::OpenMvBridgeNode>(&openmv_serial);
    if (!app.addNode(openmv_node)) {
        ESP_LOGE(TAG, "Failed to add OpenMvBridgeNode");
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
    if (!driver_manager.registerDriver(&drive_left, 30) || !driver_manager.registerDriver(&drive_right, 30) ||
        !driver_manager.registerDriver(&dome_motor, 30) || !driver_manager.registerDriver(&maestro_body, 40) ||
        !driver_manager.registerDriver(&maestro_dome, 41) || !driver_manager.registerDriver(&mp3, 50)) {
        ESP_LOGE(TAG, "Failed to register one or more HAL drivers");
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
    ESP_LOGI(TAG, "Runtime started: Bluepad on BT task, Application executor on core %d", exec_cfg.executor_task_core);
    return 0;
}
