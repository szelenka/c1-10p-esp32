#include "chopper/core/TimerManager.h"
#include "esp_log.h"
#include <cstring>

static const char* const TAG = "TimerManager";

namespace chopper::core {

TimerManager& TimerManager::getInstance() {
    static TimerManager instance;
    return instance;
}

TimerManager::TimerManager() {
    memset(timers_, 0, sizeof(timers_));
}

uint32_t TimerManager::createTimer(uint32_t period_us, TimerCallback callback, void* context, bool one_shot) {
    if (callback == nullptr) {
        ESP_LOGE(TAG, "Cannot create timer with null callback");
        return 0;
    }
    if (period_us == 0) {
        ESP_LOGE(TAG, "Cannot create timer with zero period");
        return 0;
    }

    for (auto& timer : timers_) {
        if (!timer.active) {
            timer.id = next_id_++;
            timer.period_us = period_us;
            timer.next_fire_us = 0;  // Will be set on first tick
            timer.callback = callback;
            timer.context = context;
            timer.one_shot = one_shot;
            timer.active = true;
            ESP_LOGI(TAG, "Created timer id=%u period=%u us one_shot=%d", timer.id, period_us, (int)one_shot);
            return timer.id;
        }
    }

    ESP_LOGE(TAG, "Timer array full (max %zu)", limits::MAX_TIMERS);
    return 0;
}

void TimerManager::cancelTimer(uint32_t id) {
    for (auto& timer : timers_) {
        if (timer.active && timer.id == id) {
            timer.active = false;
            ESP_LOGI(TAG, "Cancelled timer id=%u", id);
            return;
        }
    }
    ESP_LOGW(TAG, "cancelTimer: id=%u not found", id);
}

void TimerManager::resetTimer(uint32_t id, uint64_t now_us) {
    for (auto& timer : timers_) {
        if (timer.active && timer.id == id) {
            timer.next_fire_us = now_us + timer.period_us;
            return;
        }
    }
    ESP_LOGW(TAG, "resetTimer: id=%u not found", id);
}

void TimerManager::tick(uint64_t now_us) {
    for (auto& t : timers_) {
        if (!t.active) {
            continue;
        }

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
    for (const auto& timer : timers_) {
        if (timer.active) {
            ++count;
        }
    }
    return count;
}

}  // namespace chopper::core
