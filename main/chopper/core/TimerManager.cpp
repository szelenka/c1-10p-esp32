#include "chopper/core/TimerManager.h"
#include "esp_log.h"
#include <cstring>

static const char* TAG = "TimerManager";

namespace chopper {
namespace core {

TimerManager& TimerManager::getInstance() {
    static TimerManager instance;
    return instance;
}

TimerManager::TimerManager() : next_id_(1) {
    memset(timers_, 0, sizeof(timers_));
}

uint32_t TimerManager::createTimer(uint32_t period_us, TimerCallback callback,
                                   void* context, bool one_shot) {
    if (!callback) {
        ESP_LOGE(TAG, "Cannot create timer with null callback");
        return 0;
    }
    if (period_us == 0) {
        ESP_LOGE(TAG, "Cannot create timer with zero period");
        return 0;
    }

    for (size_t i = 0; i < limits::MAX_TIMERS; ++i) {
        if (!timers_[i].active) {
            timers_[i].id           = next_id_++;
            timers_[i].period_us    = period_us;
            timers_[i].next_fire_us = 0; // Will be set on first tick
            timers_[i].callback     = callback;
            timers_[i].context      = context;
            timers_[i].one_shot     = one_shot;
            timers_[i].active       = true;
            ESP_LOGI(TAG, "Created timer id=%u period=%u us one_shot=%d",
                     timers_[i].id, period_us, (int)one_shot);
            return timers_[i].id;
        }
    }

    ESP_LOGE(TAG, "Timer array full (max %zu)", limits::MAX_TIMERS);
    return 0;
}

void TimerManager::cancelTimer(uint32_t id) {
    for (size_t i = 0; i < limits::MAX_TIMERS; ++i) {
        if (timers_[i].active && timers_[i].id == id) {
            timers_[i].active = false;
            ESP_LOGI(TAG, "Cancelled timer id=%u", id);
            return;
        }
    }
    ESP_LOGW(TAG, "cancelTimer: id=%u not found", id);
}

void TimerManager::resetTimer(uint32_t id, uint64_t now_us) {
    for (size_t i = 0; i < limits::MAX_TIMERS; ++i) {
        if (timers_[i].active && timers_[i].id == id) {
            timers_[i].next_fire_us = now_us + timers_[i].period_us;
            return;
        }
    }
    ESP_LOGW(TAG, "resetTimer: id=%u not found", id);
}

void TimerManager::tick(uint64_t now_us) {
    for (size_t i = 0; i < limits::MAX_TIMERS; ++i) {
        TimerEntry& t = timers_[i];
        if (!t.active) continue;

        // First tick: initialise next_fire_us
        if (t.next_fire_us == 0) {
            t.next_fire_us = now_us + t.period_us;
            continue;
        }

        if (now_us >= t.next_fire_us) {
            t.callback(t.context);

            if (t.one_shot) {
                t.active = false;
            } else {
                t.next_fire_us += t.period_us;
                // Guard against drift: if we missed multiple periods, skip ahead
                if (t.next_fire_us < now_us) {
                    t.next_fire_us = now_us + t.period_us;
                }
            }
        }
    }
}

size_t TimerManager::activeCount() const {
    size_t count = 0;
    for (size_t i = 0; i < limits::MAX_TIMERS; ++i) {
        if (timers_[i].active) ++count;
    }
    return count;
}

} // namespace core
} // namespace chopper
