#pragma once
#include <cstdint>
#include <chrono>

// Settable mock clock for deterministic tests.
// When mock_esp_timer_us > 0, esp_timer_get_time() returns that value.
// Otherwise falls back to real wall clock.
inline int64_t& mock_esp_timer_us_ref() {
    static int64_t value = 0;
    return value;
}

inline void mock_esp_timer_set(int64_t us) {
    mock_esp_timer_us_ref() = us;
}

inline void mock_esp_timer_reset() {
    mock_esp_timer_us_ref() = 0;
}

inline int64_t esp_timer_get_time() {
    int64_t mock_val = mock_esp_timer_us_ref();
    if (mock_val > 0) {
        return mock_val;
    }
    auto now = std::chrono::steady_clock::now().time_since_epoch();
    return std::chrono::duration_cast<std::chrono::microseconds>(now).count();
}
