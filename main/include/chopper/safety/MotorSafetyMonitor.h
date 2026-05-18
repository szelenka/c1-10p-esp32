#pragma once

#include <cstdint>
#include <cstddef>
#include <atomic>
#include "chopper/chopper_limits.h"
#include "chopper/safety/ErrorLog.h"
#include "esp_timer.h"
#include "esp_log.h"

namespace chopper::safety {

/**
 * @brief ISR-safe per-motor watchdog monitor.
 *
 * Replaces the WPILib MotorSafety with a FreeRTOS-native, lock-free design.
 * Each motor slot has an atomic last_feed_time. The checkAll() method runs
 * every executor tick and fires stop callbacks for timed-out motors.
 *
 * No heap allocation. No mutexes. Safe to call feed() from ISR context.
 */
class MotorSafetyMonitor {
public:
    static constexpr size_t MAX_MOTORS = limits::MAX_MOTORS;

    struct MotorEntry {
        uint8_t id = 0;
        const char* name = nullptr;
        uint32_t timeout_us = 0;
        std::atomic<uint64_t> last_feed_us{0};
        bool enabled = false;
        bool timed_out = false;
        bool active = false;

        // Statistics
        uint32_t timeout_count = 0;
        uint32_t feed_count = 0;
        uint64_t max_feed_interval_us = 0;
    };

    using StopCallback = void (*)(uint8_t motor_id, void* context);

    MotorSafetyMonitor() = default;

    /**
     * @brief Register a motor with its stop callback.
     * @return Motor ID (slot index), or 0xFF on failure.
     */
    uint8_t registerMotor(const char* name, uint32_t timeout_ms, StopCallback stop_cb, void* ctx) {
        for (size_t i = 0; i < MAX_MOTORS; ++i) {
            if (!motors_[i].active) {
                motors_[i].id = static_cast<uint8_t>(i);
                motors_[i].name = name;
                motors_[i].timeout_us = timeout_ms * 1000u;
                motors_[i].last_feed_us.store(static_cast<uint64_t>(esp_timer_get_time()), std::memory_order_relaxed);
                motors_[i].enabled = true;
                motors_[i].timed_out = false;
                motors_[i].active = true;
                motors_[i].timeout_count = 0;
                motors_[i].feed_count = 0;
                motors_[i].max_feed_interval_us = 0;

                stop_callbacks_[i].callback = stop_cb;
                stop_callbacks_[i].context = ctx;
                motor_count_++;
                return static_cast<uint8_t>(i);
            }
        }
        return 0xFF;
    }

    /// Unregister a motor, freeing its slot.
    void unregisterMotor(uint8_t id) {
        if (id < MAX_MOTORS && motors_[id].active) {
            motors_[id].active = false;
            motors_[id].enabled = false;
            stop_callbacks_[id].callback = nullptr;
            stop_callbacks_[id].context = nullptr;
            if (motor_count_ > 0) {
                motor_count_--;
            }
        }
    }

    /**
     * @brief Feed the watchdog for a specific motor. ISR-safe.
     *
     * Must be called every time a motor command is sent. Resets the
     * timeout timer for this motor.
     */
    void feed(uint8_t motor_id) {
        if (motor_id < MAX_MOTORS && motors_[motor_id].active) {
            auto now = static_cast<uint64_t>(esp_timer_get_time());
            uint64_t last = motors_[motor_id].last_feed_us.load(std::memory_order_relaxed);
            uint64_t interval = now - last;
            if (interval > motors_[motor_id].max_feed_interval_us) {
                motors_[motor_id].max_feed_interval_us = interval;
            }
            motors_[motor_id].last_feed_us.store(now, std::memory_order_relaxed);
            motors_[motor_id].feed_count++;
        }
    }

    /**
     * @brief Check all motors for timeout. Called by executor every tick.
     *
     * For each active, enabled motor that has exceeded its timeout and
     * has not already been marked timed_out, fires the stop callback
     * and logs an error.
     */
    void checkAll(uint64_t now_us) {
        for (size_t i = 0; i < MAX_MOTORS; ++i) {
            if (!motors_[i].active || !motors_[i].enabled) {
                continue;
            }
            uint64_t last = motors_[i].last_feed_us.load(std::memory_order_relaxed);
            uint64_t elapsed = now_us - last;
            if (elapsed > motors_[i].timeout_us && !motors_[i].timed_out) {
                motors_[i].timed_out = true;
                motors_[i].timeout_count++;
                if (stop_callbacks_[i].callback != nullptr) {
                    stop_callbacks_[i].callback(static_cast<uint8_t>(i), stop_callbacks_[i].context);
                }
                ErrorLog::getActive().log(ErrorLog::MOTOR_TIMEOUT, static_cast<uint8_t>(i),
                                          static_cast<uint32_t>(elapsed));
            }
        }
    }

    /**
     * @brief Reset a timed-out motor. Requires explicit action.
     * @return true if motor was reset, false if not found or not timed out.
     */
    bool resetMotor(uint8_t motor_id) {
        if (motor_id >= MAX_MOTORS || !motors_[motor_id].active) {
            return false;
        }
        if (!motors_[motor_id].timed_out) {
            return false;
        }
        motors_[motor_id].timed_out = false;
        motors_[motor_id].last_feed_us.store(static_cast<uint64_t>(esp_timer_get_time()), std::memory_order_relaxed);
        return true;
    }

    /// Get read-only access to a motor entry. Returns nullptr if invalid.
    const MotorEntry* getMotor(uint8_t motor_id) const {
        if (motor_id >= MAX_MOTORS || !motors_[motor_id].active) {
            return nullptr;
        }
        return &motors_[motor_id];
    }

    /// Disable all motors immediately. Fires all stop callbacks.
    void disableAll() {
        auto now = static_cast<uint64_t>(esp_timer_get_time());
        for (size_t i = 0; i < MAX_MOTORS; ++i) {
            if (!motors_[i].active) {
                continue;
            }
            motors_[i].enabled = false;
            if (stop_callbacks_[i].callback != nullptr) {
                stop_callbacks_[i].callback(static_cast<uint8_t>(i), stop_callbacks_[i].context);
            }
        }
    }

    /// Get number of registered motors.
    size_t getMotorCount() const { return motor_count_; }

    /// Check if any motor has timed out.
    bool hasAnyTimeout() const {
        for (const auto& motor : motors_) {
            if (motor.active && motor.timed_out) {
                return true;
            }
        }
        return false;
    }

private:
    MotorEntry motors_[MAX_MOTORS] = {};

    struct StopCallbackEntry {
        StopCallback callback = nullptr;
        void* context = nullptr;
    };
    StopCallbackEntry stop_callbacks_[MAX_MOTORS] = {};
    size_t motor_count_ = 0;

public:
    MotorSafetyMonitor(const MotorSafetyMonitor&) = delete;
    MotorSafetyMonitor& operator=(const MotorSafetyMonitor&) = delete;
};

}  // namespace chopper::safety
