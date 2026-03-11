#pragma once

#include <cstdint>
#include <cstddef>
#include "chopper/chopper_limits.h"

namespace chopper::core {

/// Callback type for timer expiry.
using TimerCallback = void (*)(void* context);

/// A single timer entry in the fixed-size array.
struct TimerEntry {
    uint32_t id;
    uint32_t period_us;
    uint64_t next_fire_us;
    TimerCallback callback;
    void* context;
    bool one_shot;
    bool active;
};

/**
 * @brief Lightweight timer manager using polled fixed-size array.
 *
 * No FreeRTOS software timers (they heap-allocate). Instead, the executor
 * calls tick() each loop iteration and expired timers fire their callbacks.
 */
class TimerManager {
public:
    static TimerManager& getInstance();

    /**
     * @brief Create a timer.
     * @param period_us Timer period in microseconds.
     * @param callback  Function pointer called on expiry.
     * @param context   Opaque context passed to callback.
     * @param one_shot  If true, the timer fires once then deactivates.
     * @return Timer ID (>0 on success, 0 on failure / array full).
     */
    uint32_t createTimer(uint32_t period_us, TimerCallback callback, void* context, bool one_shot = false);

    /**
     * @brief Cancel a timer by ID. The slot is marked inactive and may be reused.
     */
    void cancelTimer(uint32_t id);

    /**
     * @brief Reset a timer's deadline to now + period.
     * @param id    Timer ID.
     * @param now_us Current time in microseconds.
     */
    void resetTimer(uint32_t id, uint64_t now_us);

    /**
     * @brief Fire all due timers. Called by the executor each loop iteration.
     * @param now_us Current timestamp from esp_timer_get_time().
     */
    void tick(uint64_t now_us);

    /// Number of currently active timers.
    [[nodiscard]] size_t activeCount() const;

private:
    TimerManager();

    TimerEntry timers_[limits::MAX_TIMERS]{};
    uint32_t next_id_ = 1;

public:
    TimerManager(const TimerManager&) = delete;
    TimerManager& operator=(const TimerManager&) = delete;
};

}  // namespace chopper::core
