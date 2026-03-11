#pragma once

#include "chopper/core/PublishingNode.h"
#include "chopper/core/ParameterServer.h"
#include "chopper/hal/DriverManager.h"
#include "esp_log.h"
#include "esp_timer.h"

namespace chopper::nodes {

/**
 * Ticks the HAL DriverManager from the executor loop.
 *
 * Bridge/control nodes stage desired values into driver state, and HAL drivers
 * perform bus I/O in update(). This node executes that periodic update step.
 */
class DriverUpdateNode : public core::PublishingNode {
public:
    explicit DriverUpdateNode(double hz = 50.0) : PublishingNode("driver_update"), hz_(hz) {
        // Serial-backed HAL updates can block (especially SoftwareSerial).
        // Use a realistic budget to avoid false node-timeout E-stops.
        setMaxExecutionTime(50000);
    }

    bool initialize() override {
        auto& ps = core::ParameterServer::getInstance();
        if (!ps.get(kTimingLogParamName, timing_logs_enabled_)) {
            ps.declare(kTimingLogParamName, false);
            (void)ps.get(kTimingLogParamName, timing_logs_enabled_);
        }
        (void)ps.onChange(kTimingLogParamName, &DriverUpdateNode::onParamChanged, this);

        last_tick_us_ = static_cast<uint64_t>(esp_timer_get_time());
        last_log_us_ = last_tick_us_;
        heartbeat_count_ = 0;
        ESP_LOGI(kTag, "DriverUpdateNode initialized at %.1f Hz (timing_logs=%d)", hz_, timing_logs_enabled_ ? 1 : 0);
        return true;
    }

    void process(uint64_t) override {
        const auto now_us = static_cast<uint64_t>(esp_timer_get_time());
        if (first_tick_) {
            first_tick_ = false;
            last_tick_us_ = now_us;
            last_log_us_ = now_us;
            hal::DriverManager::getInstance().updateAll();
            return;
        }

        const uint64_t dt_us = now_us - last_tick_us_;
        last_tick_us_ = now_us;
        heartbeat_count_++;

        // Warn if loop stalls well beyond the nominal 50 Hz update cadence.
        if (timing_logs_enabled_ && dt_us > kStallWarnUs) {
            ESP_LOGW(kTag, "driver update heartbeat stall: dt=%llu us", static_cast<unsigned long long>(dt_us));
        }

        hal::DriverManager::getInstance().updateAll();

        // Heartbeat summary at 1 Hz to prove runtime update loop is alive.
        if (timing_logs_enabled_ && (now_us - last_log_us_) >= kHeartbeatLogPeriodUs) {
            const uint64_t elapsed_us = now_us - last_log_us_;
            const float hz = (elapsed_us > 0)
                                 ? (static_cast<float>(heartbeat_count_) * 1000000.0f / static_cast<float>(elapsed_us))
                                 : 0.0f;
            ESP_LOGI(kTag, "driver update heartbeat: ticks=%u elapsed=%llums rate=%.1fHz",
                     static_cast<unsigned>(heartbeat_count_), static_cast<unsigned long long>(elapsed_us / 1000ULL),
                     hz);
            const auto& mgr = hal::DriverManager::getInstance();
            const uint8_t count = mgr.getDriverCount();
            for (uint8_t i = 0; i < count; i++) {
                const auto* entry = mgr.getEntry(i);
                if ((entry == nullptr) || (entry->driver == nullptr)) {
                    continue;
                }
                ESP_LOGI(kTag, "driver timing: idx=%u name=%s pri=%u last=%lluus worst=%lluus",
                         static_cast<unsigned>(i), entry->driver->getName(), static_cast<unsigned>(entry->priority),
                         static_cast<unsigned long long>(entry->lastUpdateUs),
                         static_cast<unsigned long long>(entry->worstCaseUs));
            }
            last_log_us_ = now_us;
            heartbeat_count_ = 0;
        }
    }

    void emergencyStop() override {}

    [[nodiscard]] double getUpdateFrequency() const override { return hz_; }

private:
    static constexpr const char* kTag = "DriverUpdate";
    static constexpr const char* kTimingLogParamName = "diag.driver_timing_logs";
    static constexpr uint64_t kHeartbeatLogPeriodUs = 1000000ULL;
    static constexpr uint64_t kStallWarnUs = 100000ULL;

    static void onParamChanged(const char*, void* context) {
        if (context == nullptr) {
            return;
        }
        auto* self = static_cast<DriverUpdateNode*>(context);
        auto& ps = core::ParameterServer::getInstance();
        bool enabled = self->timing_logs_enabled_;
        if (ps.get(kTimingLogParamName, enabled)) {
            self->timing_logs_enabled_ = enabled;
            ESP_LOGI(kTag, "driver timing logs %s", self->timing_logs_enabled_ ? "enabled" : "disabled");
            if (!self->timing_logs_enabled_) {
                self->heartbeat_count_ = 0;
                self->last_log_us_ = static_cast<uint64_t>(esp_timer_get_time());
            }
        }
    }

    double hz_;
    uint64_t last_tick_us_ = 0;
    uint64_t last_log_us_ = 0;
    uint32_t heartbeat_count_ = 0;
    bool first_tick_ = true;
    bool timing_logs_enabled_ = true;
};

}  // namespace chopper::nodes
