#include "chopper/telemetry/TelemetryService.h"
#include "chopper/core/ParameterServer.h"

#if defined(__has_include)
#if __has_include("sdkconfig.h")
#include "sdkconfig.h"
#endif
#endif
#include "esp_log.h"
#ifdef ESP_PLATFORM
#include "esp_timer.h"
#endif

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdarg>
#include <chrono>

static const char* const TAG = "TelemetrySvc";

namespace chopper::telemetry {

#if CHOPPER_HAS_HTTP_SERVER && defined(CONFIG_HTTPD_WS_SUPPORT)
#define CHOPPER_HAS_HTTP_WS 1
#else
#define CHOPPER_HAS_HTTP_WS 0
#endif

namespace {
void defaultSerialSink(const char* line, void*) {
    std::printf("%s\n", line);
}

uint64_t monotonicNowUs() {
#ifdef ESP_PLATFORM
    return static_cast<uint64_t>(esp_timer_get_time());
#else
    using namespace std::chrono;
    return static_cast<uint64_t>(duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count());
#endif
}
}  // namespace

TelemetryService::TelemetryService()
    : serial_sink_(&defaultSerialSink)
#ifdef ESP_PLATFORM
    , async_task_handle_(nullptr)
    , async_queue_(nullptr)
#endif
#if CHOPPER_HAS_HTTP_SERVER
    , http_server_(nullptr)
#endif
{
    std::memset(&snapshot_, 0, sizeof(snapshot_));
    std::memset(&input_states_, 0, sizeof(input_states_));
    std::memset(&motor_states_, 0, sizeof(motor_states_));
    std::memset(&servo_states_, 0, sizeof(servo_states_));
    std::memset(&led_state_, 0, sizeof(led_state_));
    std::memset(&audio_state_, 0, sizeof(audio_state_));
    std::memset(&status_state_, 0, sizeof(status_state_));
    std::memset(&async_pending_frame_, 0, sizeof(async_pending_frame_));
    std::memset(&publish_frame_, 0, sizeof(publish_frame_));
    std::memset(last_json_, 0, sizeof(last_json_));
    std::memset(json_work_, 0, sizeof(json_work_));
    std::memset(serial_line_, 0, sizeof(serial_line_));
    std::memset(ws_line_, 0, sizeof(ws_line_));
}

bool TelemetryService::begin(const Config& config) {
    config_ = config;
#if CHOPPER_HAS_HTTP_SERVER
    // Network telemetry is primarily for diagnostics; include perf fields there.
    if ((config_.http_enabled || config_.websocket_enabled) && !config_.include_perf) {
        config_.include_perf = true;
    }
#endif
    running_ = true;
    last_publish_us_ = 0;
    {
        std::lock_guard<std::mutex> lock(json_mutex_);
        std::snprintf(last_json_, sizeof(last_json_), "{}");
    }

#ifdef ESP_PLATFORM
    if (config_.async_enabled && !startAsyncTask()) {
        ESP_LOGW(TAG, "Async telemetry task failed to start; falling back to synchronous publish");
        config_.async_enabled = false;
    }
#else
    config_.async_enabled = false;
#endif

#if CHOPPER_HAS_HTTP_SERVER
    if ((config_.http_enabled || config_.websocket_enabled) && !startHttpServer()) {
        ESP_LOGW(TAG, "HTTP server did not start; telemetry continues over serial");
    }
#else
    if (config_.http_enabled || config_.websocket_enabled) {
        ESP_LOGW(TAG, "HTTP/WebSocket telemetry unavailable in this build");
    }
#endif

    ESP_LOGI(TAG, "Telemetry service started (serial=%d compact=%d async=%d core=%d, http=%d, ws=%d perf=%d)",
             config_.serial_enabled ? 1 : 0, config_.serial_compact ? 1 : 0, config_.async_enabled ? 1 : 0,
             static_cast<int>(config_.async_task_core), config_.http_enabled ? 1 : 0, config_.websocket_enabled ? 1 : 0,
             config_.include_perf ? 1 : 0);
    return true;
}

void TelemetryService::shutdown() {
#if CHOPPER_HAS_HTTP_SERVER
    stopHttpServer();
#endif
#ifdef ESP_PLATFORM
    stopAsyncTask();
#endif
    running_ = false;
}

void TelemetryService::setSerialSink(SerialSink sink, void* context) {
    serial_sink_ = (sink != nullptr) ? sink : &defaultSerialSink;
    serial_sink_context_ = context;
}

void TelemetryService::update(const Snapshot& snapshot) {
    if (!running_) {
        return;
    }

    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        snapshot_ = snapshot;
    }

    uint64_t min_interval_us = static_cast<uint64_t>(config_.min_publish_interval_ms) * 1000ULL;
    if (min_interval_us > 0 && last_publish_us_ > 0 && (snapshot.timestamp_us - last_publish_us_) < min_interval_us) {
        return;
    }

    last_publish_us_ = snapshot.timestamp_us;
#ifdef ESP_PLATFORM
    if (config_.async_enabled && async_queue_) {
        uint8_t signal = 1;
        if (state_mutex_.try_lock()) {
            snapshotToFrame(async_pending_frame_);
            state_mutex_.unlock();
            // Queue depth is 1; overwrite keeps producer non-blocking.
            (void)xQueueOverwrite(async_queue_, &signal);
        }
        return;
    }
#endif

    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        snapshotToFrame(publish_frame_);
    }
    publishFrame(publish_frame_);
}

void TelemetryService::observeInput(InputRole role, const messages::ControllerInput& input) {
    const size_t idx = static_cast<size_t>(role);
    if (idx >= static_cast<size_t>(InputRole::COUNT)) {
        return;
    }
    std::lock_guard<std::mutex> lock(state_mutex_);
    auto& st = input_states_[idx];
    const uint8_t prev_dpad = st.dpad;
    const int32_t prev_axis_x = st.axis_x;
    const int32_t prev_axis_y = st.axis_y;
    const int32_t prev_axis_rx = st.axis_rx;
    const int32_t prev_axis_ry = st.axis_ry;
    const uint16_t prev_buttons = st.buttons;
    const uint16_t prev_misc = st.misc_buttons;

    st.valid = true;
    st.connected = input.is_connected;
    st.battery = input.battery_level;
    st.dpad = input.dpad;
    st.axis_x = input.axis_x;
    st.axis_y = input.axis_y;
    st.axis_rx = input.axis_rx;
    st.axis_ry = input.axis_ry;
    st.buttons = input.buttons;
    st.misc_buttons = input.misc_buttons;
    st.report_count++;

    const auto button_edges = static_cast<uint16_t>(prev_buttons ^ st.buttons);
    if (button_edges != 0) {
        st.button_edge_mask = static_cast<uint16_t>(st.button_edge_mask | button_edges);
        st.button_edge_count++;
    }

    const bool changed = (prev_dpad != st.dpad) || (prev_axis_x != st.axis_x) || (prev_axis_y != st.axis_y) ||
                         (prev_axis_rx != st.axis_rx) || (prev_axis_ry != st.axis_ry) || (prev_buttons != st.buttons) ||
                         (prev_misc != st.misc_buttons);
    if (changed) {
        st.change_count++;
        st.last_change_us = monotonicNowUs();
    }
}

void TelemetryService::observeMotorCommand(const messages::MotorCommand& cmd) {
    if (cmd.motor_id >= limits::MAX_MOTORS) {
        return;
    }
    std::lock_guard<std::mutex> lock(state_mutex_);
    auto& st = motor_states_[cmd.motor_id];
    st.valid = true;
    st.command_type = static_cast<uint8_t>(cmd.command_type);
    st.value = cmd.value;
}

void TelemetryService::observeServoCommand(const messages::ServoCommand& cmd, ServoSourceGroup group) {
    if (cmd.servo_id >= limits::MAX_MOTORS) {
        return;
    }
    const size_t group_idx = static_cast<size_t>(group);
    if (group_idx >= static_cast<size_t>(ServoSourceGroup::COUNT)) {
        return;
    }
    std::lock_guard<std::mutex> lock(state_mutex_);
    auto& st = servo_states_[group_idx][cmd.servo_id];
    st.valid = true;
    st.command_type = static_cast<uint8_t>(cmd.command_type);
    st.value = cmd.value;
    st.duration_ms = cmd.duration_ms;
}

void TelemetryService::observeLedCommand(const messages::LEDCommand& cmd) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    auto& st = led_state_;
    st.valid = true;
    st.command_type = static_cast<uint8_t>(cmd.command_type);
    st.led_id = cmd.led_id;
    st.red = cmd.color.red;
    st.green = cmd.color.green;
    st.blue = cmd.color.blue;
    st.white = cmd.color.white;
    st.brightness = cmd.brightness;
    st.pattern_id = cmd.pattern_id;
    if (cmd.command_type == messages::LEDCommand::CommandType::TURN_OFF) {
        st.is_on = false;
    } else if (cmd.command_type == messages::LEDCommand::CommandType::TURN_ON) {
        st.is_on = true;
    }
}

void TelemetryService::observeAudioCommand(const messages::AudioCommand& cmd) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    audio_state_.valid = true;
    audio_state_.command_type = static_cast<uint8_t>(cmd.command_type);
    audio_state_.track_id = cmd.track_id;
    audio_state_.volume = cmd.volume;
    audio_state_.loop = cmd.loop;
}

void TelemetryService::observeSystemStatus(const messages::SystemStatus& status) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    status_state_.valid = true;
    status_state_.status = static_cast<uint8_t>(status.system_status);
    status_state_.error_code = status.error_code;
}

void TelemetryService::emitSerial(const char* json) {
    if (serial_sink_ == nullptr) {
        return;
    }

    std::snprintf(serial_line_, sizeof(serial_line_), "TEL:%s", (json != nullptr) ? json : "{}");
    serial_sink_(serial_line_, serial_sink_context_);
}

void TelemetryService::snapshotToFrame(PublishFrame& out) {
    out.snapshot = snapshot_;
    std::memcpy(out.input_states, input_states_, sizeof(input_states_));
    std::memcpy(out.motor_states, motor_states_, sizeof(motor_states_));
    std::memcpy(out.servo_states, servo_states_, sizeof(servo_states_));
    out.led_state = led_state_;
    out.audio_state = audio_state_;
    out.status_state = status_state_;
}

void TelemetryService::emitSerialCompact(const PublishFrame& frame) {
    if (serial_sink_ == nullptr) {
        return;
    }

    const auto& drive = frame.input_states[static_cast<size_t>(InputRole::DRIVE)];
    const auto& dome = frame.input_states[static_cast<size_t>(InputRole::DOME)];
    char line[512];
    if (config_.include_perf) {
        std::snprintf(line, sizeof(line),
                      "TEL:t=%llu lc=%llu loop_max=%llu loop_avg=%llu mode=%u estop=%u/%u "
                      "drv=%u d_btn=0x%04x d_edge=0x%04x d_dpad=0x%02x d_ax=(%ld,%ld,%ld,%ld) "
                      "dome=%u m_btn=0x%04x m_edge=0x%04x m_dpad=0x%02x m_ax=(%ld,%ld,%ld,%ld)",
                      static_cast<unsigned long long>(frame.snapshot.timestamp_us),
                      static_cast<unsigned long long>(frame.snapshot.loop_count),
                      static_cast<unsigned long long>(frame.snapshot.max_loop_time_us),
                      static_cast<unsigned long long>(frame.snapshot.avg_loop_time_us),
                      static_cast<unsigned>(frame.snapshot.degradation_mode), frame.snapshot.executor_estop ? 1U : 0U,
                      frame.snapshot.safety_estop ? 1U : 0U, drive.connected ? 1U : 0U,
                      static_cast<unsigned>(drive.buttons), static_cast<unsigned>(drive.button_edge_mask),
                      static_cast<unsigned>(drive.dpad), static_cast<long>(drive.axis_x),
                      static_cast<long>(drive.axis_y), static_cast<long>(drive.axis_rx),
                      static_cast<long>(drive.axis_ry), dome.connected ? 1U : 0U, static_cast<unsigned>(dome.buttons),
                      static_cast<unsigned>(dome.button_edge_mask), static_cast<unsigned>(dome.dpad),
                      static_cast<long>(dome.axis_x), static_cast<long>(dome.axis_y), static_cast<long>(dome.axis_rx),
                      static_cast<long>(dome.axis_ry));
    } else {
        std::snprintf(line, sizeof(line),
                      "TEL:t=%llu mode=%u estop=%u/%u "
                      "drv=%u d_btn=0x%04x d_edge=0x%04x d_dpad=0x%02x d_ax=(%ld,%ld,%ld,%ld) "
                      "dome=%u m_btn=0x%04x m_edge=0x%04x m_dpad=0x%02x m_ax=(%ld,%ld,%ld,%ld)",
                      static_cast<unsigned long long>(frame.snapshot.timestamp_us),
                      static_cast<unsigned>(frame.snapshot.degradation_mode), frame.snapshot.executor_estop ? 1U : 0U,
                      frame.snapshot.safety_estop ? 1U : 0U, drive.connected ? 1U : 0U,
                      static_cast<unsigned>(drive.buttons), static_cast<unsigned>(drive.button_edge_mask),
                      static_cast<unsigned>(drive.dpad), static_cast<long>(drive.axis_x),
                      static_cast<long>(drive.axis_y), static_cast<long>(drive.axis_rx),
                      static_cast<long>(drive.axis_ry), dome.connected ? 1U : 0U, static_cast<unsigned>(dome.buttons),
                      static_cast<unsigned>(dome.button_edge_mask), static_cast<unsigned>(dome.dpad),
                      static_cast<long>(dome.axis_x), static_cast<long>(dome.axis_y), static_cast<long>(dome.axis_rx),
                      static_cast<long>(dome.axis_ry));
    }
    serial_sink_(line, serial_sink_context_);
}

void TelemetryService::formatJsonFromFrame(const PublishFrame& frame, char* out_json, size_t out_len) const {
    auto appendf = [&](size_t& used, const char* fmt, ...) {
        if (!out_json || out_len == 0 || used >= out_len) {
            return;
        }
        va_list args;
        va_start(args, fmt);
        int n = std::vsnprintf(out_json + used, out_len - used, fmt, args);
        va_end(args);
        if (n <= 0) {
            return;
        }
        size_t added = static_cast<size_t>(n);
        if (used + added >= out_len) {
            used = out_len - 1;
            return;
        }
        used += added;
    };

    size_t used = 0;
    appendf(used, "{\"timestamp_us\":%llu,\"executor_estop\":%s,\"safety_estop\":%s,\"degradation_mode\":%u",
            static_cast<unsigned long long>(frame.snapshot.timestamp_us),
            frame.snapshot.executor_estop ? "true" : "false", frame.snapshot.safety_estop ? "true" : "false",
            static_cast<unsigned>(frame.snapshot.degradation_mode));

    if (config_.include_perf) {
        appendf(used,
                ",\"loop_count\":%llu,\"max_loop_time_us\":%llu,"
                "\"avg_loop_time_us\":%llu,\"active_nodes\":%u,\"total_nodes\":%u",
                static_cast<unsigned long long>(frame.snapshot.loop_count),
                static_cast<unsigned long long>(frame.snapshot.max_loop_time_us),
                static_cast<unsigned long long>(frame.snapshot.avg_loop_time_us),
                static_cast<unsigned>(frame.snapshot.active_nodes), static_cast<unsigned>(frame.snapshot.total_nodes));
    }

    const char* const role_keys[] = {"drive", "dome", "animation", "camera"};
    appendf(used, ",\"inputs\":{");
    for (size_t i = 0; i < static_cast<size_t>(InputRole::COUNT); ++i) {
        const auto& st = frame.input_states[i];
        appendf(used,
                "%s\"%s\":{\"valid\":%s,\"connected\":%s,\"battery\":%u,"
                "\"dpad\":%u,\"axes\":[%ld,%ld,%ld,%ld],\"buttons\":%u,\"misc\":%u,"
                "\"reports\":%u,\"changes\":%u,\"btn_edges\":%u,\"btn_edge_mask\":%u,\"last_change_us\":%llu}",
                (i == 0) ? "" : ",", role_keys[i], st.valid ? "true" : "false", st.connected ? "true" : "false",
                static_cast<unsigned>(st.battery), static_cast<unsigned>(st.dpad), static_cast<long>(st.axis_x),
                static_cast<long>(st.axis_y), static_cast<long>(st.axis_rx), static_cast<long>(st.axis_ry),
                static_cast<unsigned>(st.buttons), static_cast<unsigned>(st.misc_buttons),
                static_cast<unsigned>(st.report_count), static_cast<unsigned>(st.change_count),
                static_cast<unsigned>(st.button_edge_count), static_cast<unsigned>(st.button_edge_mask),
                static_cast<unsigned long long>(st.last_change_us));
    }
    appendf(used, "}");

    appendf(used, ",\"outputs\":{");
    appendf(used, "\"motors\":[");
    bool first = true;
    for (size_t i = 0; i < limits::MAX_MOTORS; ++i) {
        const auto& st = frame.motor_states[i];
        if (!st.valid) {
            continue;
        }
        appendf(used, "%s{\"id\":%u,\"type\":%u,\"value\":%.4f}", first ? "" : ",", static_cast<unsigned>(i),
                static_cast<unsigned>(st.command_type), static_cast<double>(st.value));
        first = false;
    }
    appendf(used, "],");

    const char* const servo_group_keys[] = {"any", "body", "dome"};
    appendf(used, "\"servos\":[");
    first = true;
    for (size_t group_idx = 0; group_idx < static_cast<size_t>(ServoSourceGroup::COUNT); ++group_idx) {
        for (size_t i = 0; i < limits::MAX_MOTORS; ++i) {
            const auto& st = frame.servo_states[group_idx][i];
            if (!st.valid) {
                continue;
            }
            appendf(used, "%s{\"id\":%u,\"type\":%u,\"value\":%.4f,\"dur_ms\":%u,\"group\":\"%s\",\"group_id\":%u}",
                    first ? "" : ",", static_cast<unsigned>(i), static_cast<unsigned>(st.command_type),
                    static_cast<double>(st.value), static_cast<unsigned>(st.duration_ms), servo_group_keys[group_idx],
                    static_cast<unsigned>(group_idx));
            first = false;
        }
    }
    appendf(used, "],");

    appendf(used,
            "\"led\":{\"valid\":%s,\"id\":%u,\"type\":%u,\"on\":%s,"
            "\"color\":{\"r\":%u,\"g\":%u,\"b\":%u,\"w\":%u},\"brightness\":%u,\"pattern\":%u},"
            "\"audio\":{\"valid\":%s,\"type\":%u,\"track\":%u,\"volume\":%u,\"loop\":%s},"
            "\"status\":{\"valid\":%s,\"state\":%u,\"error\":%u}}",
            frame.led_state.valid ? "true" : "false", static_cast<unsigned>(frame.led_state.led_id),
            static_cast<unsigned>(frame.led_state.command_type), frame.led_state.is_on ? "true" : "false",
            static_cast<unsigned>(frame.led_state.red), static_cast<unsigned>(frame.led_state.green),
            static_cast<unsigned>(frame.led_state.blue), static_cast<unsigned>(frame.led_state.white),
            static_cast<unsigned>(frame.led_state.brightness), static_cast<unsigned>(frame.led_state.pattern_id),
            frame.audio_state.valid ? "true" : "false", static_cast<unsigned>(frame.audio_state.command_type),
            static_cast<unsigned>(frame.audio_state.track_id), static_cast<unsigned>(frame.audio_state.volume),
            frame.audio_state.loop ? "true" : "false", frame.status_state.valid ? "true" : "false",
            static_cast<unsigned>(frame.status_state.status), static_cast<unsigned>(frame.status_state.error_code));

    appendf(used, "}");
}

void TelemetryService::publishFrame(const PublishFrame& frame) {
    bool need_full_json =
#if CHOPPER_HAS_HTTP_SERVER
        config_.http_enabled || config_.websocket_enabled ||
#endif
        (config_.serial_enabled && !config_.serial_compact);

    if (need_full_json) {
        std::memset(json_work_, 0, sizeof(json_work_));
        formatJsonFromFrame(frame, json_work_, sizeof(json_work_));
        {
            std::lock_guard<std::mutex> lock(json_mutex_);
            std::snprintf(last_json_, sizeof(last_json_), "%s", json_work_);
        }
        if (config_.serial_enabled && !config_.serial_compact) {
            emitSerial(json_work_);
        }
    }

    if (config_.serial_enabled && config_.serial_compact) {
        emitSerialCompact(frame);
    }

#if CHOPPER_HAS_HTTP_SERVER
    if (config_.websocket_enabled) {
        broadcastWs();
    }
#endif
}

#ifdef ESP_PLATFORM
void TelemetryService::telemetryTaskEntry(void* arg) {
    auto* self = static_cast<TelemetryService*>(arg);
    if (self) {
        self->telemetryTaskLoop();
    }
    vTaskDelete(nullptr);
}

void TelemetryService::telemetryTaskLoop() {
    uint8_t signal = 0;
    while (running_) {
        if (!async_queue_) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }
        if (xQueueReceive(async_queue_, &signal, pdMS_TO_TICKS(250)) == pdTRUE) {
            (void)signal;
            {
                std::lock_guard<std::mutex> lock(state_mutex_);
                publish_frame_ = async_pending_frame_;
            }
            publishFrame(publish_frame_);
        }
    }
    async_task_handle_ = nullptr;
}

bool TelemetryService::startAsyncTask() {
    if (async_queue_ && async_task_handle_) {
        return true;
    }

    async_queue_ = xQueueCreate(1, sizeof(uint8_t));
    if (!async_queue_) {
        return false;
    }

    BaseType_t res = pdFAIL;
    const uint32_t stack_bytes = config_.async_task_stack_words > 0 ? config_.async_task_stack_words : 4096;

    if (config_.async_task_core >= 0) {
        res = xTaskCreatePinnedToCore(&TelemetryService::telemetryTaskEntry, "telemetry_pub", stack_bytes, this,
                                      config_.async_task_priority, &async_task_handle_, config_.async_task_core);
    } else {
        res = xTaskCreate(&TelemetryService::telemetryTaskEntry, "telemetry_pub", stack_bytes, this,
                          config_.async_task_priority, &async_task_handle_);
    }

    if (res != pdPASS) {
        vQueueDelete(async_queue_);
        async_queue_ = nullptr;
        async_task_handle_ = nullptr;
        return false;
    }

    return true;
}

void TelemetryService::stopAsyncTask() {
    if (async_task_handle_) {
        vTaskDelete(async_task_handle_);
        async_task_handle_ = nullptr;
    }
    if (async_queue_) {
        vQueueDelete(async_queue_);
        async_queue_ = nullptr;
    }
}
#endif

#if CHOPPER_HAS_HTTP_SERVER
namespace {
struct ParamJsonCtx {
    char* out;
    size_t capacity;
    size_t used;
    bool first;
};

static void appendParamAsJson(const core::Parameter& p, void* ctx_ptr) {
    auto* ctx = static_cast<ParamJsonCtx*>(ctx_ptr);
    if (!ctx || ctx->used >= ctx->capacity) {
        return;
    }

    const char* fmt = nullptr;
    char value_buf[64] = {};
    switch (p.type) {
        case core::ParamType::INT32:
            std::snprintf(value_buf, sizeof(value_buf), "%d", static_cast<int>(p.value.i));
            fmt = ctx->first ? "\"%s\":%s" : ",\"%s\":%s";
            break;
        case core::ParamType::FLOAT:
            std::snprintf(value_buf, sizeof(value_buf), "%.6f", static_cast<double>(p.value.f));
            fmt = ctx->first ? "\"%s\":%s" : ",\"%s\":%s";
            break;
        case core::ParamType::BOOL:
            std::snprintf(value_buf, sizeof(value_buf), "%s", p.value.b ? "true" : "false");
            fmt = ctx->first ? "\"%s\":%s" : ",\"%s\":%s";
            break;
    }

    int n = std::snprintf(ctx->out + ctx->used, ctx->capacity - ctx->used, fmt, p.name, value_buf);
    if (n <= 0)
        return;
    size_t added = static_cast<size_t>(n);
    if (ctx->used + added >= ctx->capacity) {
        ctx->used = ctx->capacity;
        return;
    }
    ctx->used += added;
    ctx->first = false;
}
}  // namespace

bool TelemetryService::startHttpServer() {
    if (http_server_) {
        return true;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = config_.http_port;

    esp_err_t err = httpd_start(&http_server_, &config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server (%d)", static_cast<int>(err));
        http_server_ = nullptr;
        return false;
    }

    if (config_.http_enabled) {
        httpd_uri_t get_uri = {.uri = "/api/telemetry",
                               .method = HTTP_GET,
                               .handler = &TelemetryService::handleTelemetryGet,
                               .user_ctx = this};
        httpd_register_uri_handler(http_server_, &get_uri);

        httpd_uri_t params_get = {
            .uri = "/api/params", .method = HTTP_GET, .handler = &TelemetryService::handleParamsGet, .user_ctx = this};
        httpd_register_uri_handler(http_server_, &params_get);

        httpd_uri_t params_post = {.uri = "/api/params",
                                   .method = HTTP_POST,
                                   .handler = &TelemetryService::handleParamsPost,
                                   .user_ctx = this};
        httpd_register_uri_handler(http_server_, &params_post);
    }

    if (config_.websocket_enabled) {
#if CHOPPER_HAS_HTTP_WS
        httpd_uri_t ws_uri = {};
        ws_uri.uri = "/ws/telemetry";
        ws_uri.method = HTTP_GET;
        ws_uri.handler = &TelemetryService::handleTelemetryWs;
        ws_uri.user_ctx = this;
        ws_uri.is_websocket = true;
        httpd_register_uri_handler(http_server_, &ws_uri);
#else
        ESP_LOGW(TAG, "WebSocket requested but CONFIG_HTTPD_WS_SUPPORT is disabled");
#endif
    }

    return true;
}

void TelemetryService::stopHttpServer() {
    if (http_server_) {
        httpd_stop(http_server_);
        http_server_ = nullptr;
    }
}

void TelemetryService::broadcastWs() {
#if !CHOPPER_HAS_HTTP_WS
    return;
#else
    if (!http_server_) {
        return;
    }

    size_t fd_count = 8;
    int client_fds[8] = {0};
    if (httpd_get_client_list(http_server_, &fd_count, client_fds) != ESP_OK) {
        return;
    }

    {
        std::lock_guard<std::mutex> lock(json_mutex_);
        std::snprintf(ws_line_, sizeof(ws_line_), "%s", last_json_);
    }

    for (size_t i = 0; i < fd_count; ++i) {
        int fd = client_fds[i];
        if (httpd_ws_get_fd_info(http_server_, fd) != HTTPD_WS_CLIENT_WEBSOCKET) {
            continue;
        }

        httpd_ws_frame_t frame = {};
        frame.type = HTTPD_WS_TYPE_TEXT;
        frame.payload = reinterpret_cast<uint8_t*>(ws_line_);
        frame.len = std::strlen(ws_line_);
        httpd_ws_send_frame(http_server_, fd, &frame);
    }
#endif
}

esp_err_t TelemetryService::handleTelemetryGet(httpd_req_t* req) {
    auto* self = static_cast<TelemetryService*>(req->user_ctx);
    if (!self) {
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/json");
    {
        std::lock_guard<std::mutex> lock(self->json_mutex_);
        return httpd_resp_send(req, self->last_json_, HTTPD_RESP_USE_STRLEN);
    }
}

esp_err_t TelemetryService::handleTelemetryWs(httpd_req_t* req) {
#if !CHOPPER_HAS_HTTP_WS
    (void)req;
    return ESP_FAIL;
#else
    auto* self = static_cast<TelemetryService*>(req->user_ctx);
    if (!self) {
        return ESP_FAIL;
    }

    // WebSocket handshake path.
    if (req->method == HTTP_GET) {
        return ESP_OK;
    }

    httpd_ws_frame_t frame = {};
    frame.type = HTTPD_WS_TYPE_TEXT;
    {
        std::lock_guard<std::mutex> lock(self->json_mutex_);
        frame.payload = reinterpret_cast<uint8_t*>(self->last_json_);
        frame.len = std::strlen(self->last_json_);
        return httpd_ws_send_frame(req, &frame);
    }
#endif
}

esp_err_t TelemetryService::handleParamsGet(httpd_req_t* req) {
    (void)req;
    char body[2048] = "{";
    size_t used = 1;

    ParamJsonCtx ctx{.out = body, .capacity = sizeof(body), .used = used, .first = true};

    core::ParameterServer::getInstance().forEach(&appendParamAsJson, &ctx);
    used = ctx.used;

    if (used + 1 >= sizeof(body)) {
        return ESP_FAIL;
    }
    body[used++] = '}';
    body[used] = '\0';

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, body, HTTPD_RESP_USE_STRLEN);
}

esp_err_t TelemetryService::handleParamsPost(httpd_req_t* req) {
    char query[256] = {};
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK) {
        httpd_resp_set_status(req, "400 Bad Request");
        return httpd_resp_send(req, "{\"ok\":false,\"error\":\"missing query\"}", HTTPD_RESP_USE_STRLEN);
    }

    char name[96] = {};
    char value[96] = {};
    if (httpd_query_key_value(query, "name", name, sizeof(name)) != ESP_OK ||
        httpd_query_key_value(query, "value", value, sizeof(value)) != ESP_OK) {
        httpd_resp_set_status(req, "400 Bad Request");
        return httpd_resp_send(req, "{\"ok\":false,\"error\":\"name/value required\"}", HTTPD_RESP_USE_STRLEN);
    }

    auto& ps = core::ParameterServer::getInstance();
    bool updated = false;

    int32_t i = 0;
    float f = 0.0f;
    bool b = false;

    if (ps.get(name, i)) {
        char* end = nullptr;
        long parsed = std::strtol(value, &end, 10);
        if (end && *end == '\0') {
            updated = ps.set(name, static_cast<int32_t>(parsed));
        }
    } else if (ps.get(name, f)) {
        char* end = nullptr;
        float parsed = std::strtof(value, &end);
        if (end && *end == '\0') {
            updated = ps.set(name, parsed);
        }
    } else if (ps.get(name, b)) {
        if (std::strcmp(value, "1") == 0 || std::strcmp(value, "true") == 0) {
            updated = ps.set(name, true);
        } else if (std::strcmp(value, "0") == 0 || std::strcmp(value, "false") == 0) {
            updated = ps.set(name, false);
        }
    }

    if (!updated) {
        httpd_resp_set_status(req, "400 Bad Request");
        return httpd_resp_send(req, "{\"ok\":false}", HTTPD_RESP_USE_STRLEN);
    }

    return httpd_resp_send(req, "{\"ok\":true}", HTTPD_RESP_USE_STRLEN);
}
#endif

}  // namespace chopper::telemetry
