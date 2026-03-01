#pragma once

#include <cstdint>
#include <cstddef>
#include <mutex>

#include "chopper/chopper_limits.h"
#include "chopper/messages/CommonMessages.h"

#if defined(__has_include)
#if __has_include(<esp_http_server.h>)
#include <esp_http_server.h>
#define CHOPPER_HAS_HTTP_SERVER 1
#endif
#endif

#ifndef CHOPPER_HAS_HTTP_SERVER
#define CHOPPER_HAS_HTTP_SERVER 0
#endif

#ifdef ESP_PLATFORM
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#endif

namespace chopper {
namespace telemetry {

class TelemetryService {
public:
    enum class InputRole : uint8_t {
        DRIVE = 0,
        DOME = 1,
        ANIMATION = 2,
        CAMERA = 3,
        COUNT = 4
    };

    struct Config {
        bool serial_enabled = true;
        // When true, UART output uses a short fixed-width line to avoid
        // blocking the real-time executor on long JSON writes.
        bool serial_compact = true;
        // When enabled, telemetry emission runs in a dedicated task and the
        // executor only enqueues snapshots (drop/overwrite on overflow).
        // Off by default until async publisher path is fully hardened.
        bool async_enabled = false;
        int8_t async_task_core = 0;
        uint8_t async_task_priority = 3;
        // Stack size passed to xTaskCreate* (ESP-IDF interprets this as bytes).
        uint16_t async_task_stack_words = 4096;
        bool http_enabled = true;
        bool websocket_enabled = true;
        // Include executor performance fields (loop_count/loop timings/node counts).
        bool include_perf = false;
        double node_update_hz = 4.0;
        uint32_t min_publish_interval_ms = 250;
        uint16_t http_port = 80;
    };

    struct Snapshot {
        uint64_t timestamp_us = 0;
        uint64_t loop_count = 0;
        uint64_t max_loop_time_us = 0;
        uint64_t avg_loop_time_us = 0;
        uint32_t active_nodes = 0;
        uint32_t total_nodes = 0;
        bool executor_estop = false;
        bool safety_estop = false;
        uint8_t degradation_mode = 0;
    };

    using SerialSink = void(*)(const char* line, void* context);

    TelemetryService();

    bool begin(const Config& config);
    void shutdown();

    void update(const Snapshot& snapshot);
    void observeInput(InputRole role, const messages::ControllerInput& input);
    void observeMotorCommand(const messages::MotorCommand& cmd);
    void observeServoCommand(const messages::ServoCommand& cmd);
    void observeAudioCommand(const messages::AudioCommand& cmd);
    void observeSystemStatus(const messages::SystemStatus& status);

    const char* getLastJson() const { return last_json_; }
    bool isRunning() const { return running_; }

    void setSerialSink(SerialSink sink, void* context);

private:
    static constexpr size_t kMaxJsonLen = 4096;

    struct InputState {
        bool valid = false;
        bool connected = false;
        uint8_t battery = 0;
        uint8_t dpad = 0;
        int32_t axis_x = 0;
        int32_t axis_y = 0;
        int32_t axis_rx = 0;
        int32_t axis_ry = 0;
        uint16_t buttons = 0;
        uint16_t misc_buttons = 0;
        // Diagnostics: latched activity so short input taps are visible even if
        // publish cadence misses the instantaneous pressed state.
        uint32_t report_count = 0;
        uint32_t change_count = 0;
        uint32_t button_edge_count = 0;
        uint16_t button_edge_mask = 0;
        uint64_t last_change_us = 0;
    };

    struct MotorState {
        bool valid = false;
        uint8_t command_type = 0;
        float value = 0.0f;
    };

    struct ServoState {
        bool valid = false;
        uint8_t command_type = 0;
        float value = 0.0f;
        uint16_t duration_ms = 0;
    };

    struct AudioState {
        bool valid = false;
        uint8_t command_type = 0;
        uint16_t track_id = 0;
        uint8_t volume = 0;
        bool loop = false;
    };

    struct StatusState {
        bool valid = false;
        uint8_t status = 0;
        uint32_t error_code = 0;
    };

    struct PublishFrame {
        Snapshot snapshot;
        InputState input_states[static_cast<size_t>(InputRole::COUNT)];
        MotorState motor_states[limits::MAX_MOTORS];
        ServoState servo_states[limits::MAX_MOTORS];
        AudioState audio_state;
        StatusState status_state;
    };

    void emitSerial(const char* json);
    void emitSerialCompact(const PublishFrame& frame);
    void formatJsonFromFrame(const PublishFrame& frame, char* out_json, size_t out_len);
    void snapshotToFrame(PublishFrame& out);
    void publishFrame(const PublishFrame& frame);

#if CHOPPER_HAS_HTTP_SERVER
    bool startHttpServer();
    void stopHttpServer();
    void broadcastWs();

    static esp_err_t handleTelemetryGet(httpd_req_t* req);
    static esp_err_t handleTelemetryWs(httpd_req_t* req);
    static esp_err_t handleParamsGet(httpd_req_t* req);
    static esp_err_t handleParamsPost(httpd_req_t* req);

    httpd_handle_t http_server_;
#endif

#ifdef ESP_PLATFORM
    static void telemetryTaskEntry(void* arg);
    void telemetryTaskLoop();
    bool startAsyncTask();
    void stopAsyncTask();
    TaskHandle_t async_task_handle_;
    QueueHandle_t async_queue_;
#endif

    Config config_;
    Snapshot snapshot_;
    InputState input_states_[static_cast<size_t>(InputRole::COUNT)];
    MotorState motor_states_[limits::MAX_MOTORS];
    ServoState servo_states_[limits::MAX_MOTORS];
    AudioState audio_state_;
    StatusState status_state_;
    PublishFrame async_pending_frame_;
    PublishFrame publish_frame_;
    char last_json_[kMaxJsonLen];
    char json_work_[kMaxJsonLen];
    char serial_line_[kMaxJsonLen + 8];
    char ws_line_[kMaxJsonLen];
    std::mutex state_mutex_;
    std::mutex json_mutex_;
    uint64_t last_publish_us_;
    bool running_;

    SerialSink serial_sink_;
    void* serial_sink_context_;
};

} // namespace telemetry
} // namespace chopper
