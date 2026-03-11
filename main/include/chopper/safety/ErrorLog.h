#pragma once

#include <cstdint>
#include <cstddef>
#include <atomic>
#include "chopper/chopper_limits.h"
#include "esp_timer.h"
#include "esp_log.h"

namespace chopper::safety {

/**
 * @brief Lock-free ring buffer error log. ISR-safe, no heap allocation.
 *
 * Entries are written atomically using fetch_add on the write index.
 * Oldest entries are silently overwritten when the buffer wraps.
 * Designed for 128 entries x 16 bytes = 2 KB total.
 */
class ErrorLog {
public:
    static constexpr size_t LOG_SIZE = limits::MAX_ERROR_LOG_ENTRIES;

    enum Category : uint8_t {
        NONE = 0x00,
        MOTOR_TIMEOUT = 0x01,
        TIMING_WARNING = 0x02,
        TIMING_OVERRUN = 0x03,
        STACK_WARNING = 0x04,
        HEAP_WARNING = 0x05,
        BROWNOUT = 0x06,
        CONTROLLER_LOST = 0x07,
        NODE_CRASH = 0x08,
        SAFETY_ESTOP = 0x09,
        WATCHDOG_RESET = 0x0A,
        DEGRADATION = 0x0B,
        NODE_RESTART = 0x0C,
        PARAM_ERROR = 0x0D,
        BUS_ERROR = 0x0E,
        GENERIC_WARNING = 0xFE,
        GENERIC_ERROR = 0xFF,
    };

    enum Severity : uint8_t {
        DEBUG = 0,
        INFO = 1,
        WARNING = 2,
        ERROR = 3,
        FATAL = 4,
    };

    struct Entry {
        uint64_t timestamp_us;
        Category category;
        Severity severity;
        uint8_t source_id;
        uint8_t _pad;
        uint32_t detail;
    };

    static_assert(sizeof(Entry) == 16, "Entry must be 16 bytes for memory budget");

    /// Get the singleton instance.
    static ErrorLog& getInstance() {
        static ErrorLog instance;
        return instance;
    }

    /// Allow test injection of a custom instance.
    static void setInstance(ErrorLog* inst) { test_instance_ = inst; }

    /// Get the active instance (test-injected or singleton).
    static ErrorLog& getActive() { return (test_instance_ != nullptr) ? *test_instance_ : getInstance(); }

    /**
     * @brief Log an entry. Lock-free, ISR-safe.
     *
     * Uses atomic fetch_add for the write index so multiple writers
     * (including ISR context) can log concurrently without corruption.
     */
    void log(Category cat, uint8_t source, uint32_t detail, Severity sev = Severity::ERROR) {
        size_t idx = write_index_.fetch_add(1, std::memory_order_relaxed) % LOG_SIZE;
        entries_[idx].timestamp_us = getTimestamp();
        entries_[idx].category = cat;
        entries_[idx].severity = sev;
        entries_[idx].source_id = source;
        entries_[idx]._pad = 0;
        entries_[idx].detail = detail;

        total_count_.fetch_add(1, std::memory_order_relaxed);

        if (sev <= FATAL && sev < Severity::ERROR) {
            // no-op for non-error severities; count tracked below
        }
        severity_counts_[sev].fetch_add(1, std::memory_order_relaxed);
    }

    /**
     * @brief Read the most recent entries (newest first).
     * @param out     Output buffer.
     * @param max_count  Maximum entries to copy.
     * @return Number of entries actually copied.
     */
    size_t readRecent(Entry* out, size_t max_count) const {
        size_t total = total_count_.load(std::memory_order_relaxed);
        size_t available = (total < LOG_SIZE) ? total : LOG_SIZE;
        size_t count = (max_count < available) ? max_count : available;

        size_t head = write_index_.load(std::memory_order_relaxed);
        for (size_t i = 0; i < count; ++i) {
            size_t idx = (head - 1 - i) % LOG_SIZE;
            out[i] = entries_[idx];
        }
        return count;
    }

    /// Get total number of entries logged since boot (may exceed LOG_SIZE).
    uint32_t getTotalCount() const { return static_cast<uint32_t>(total_count_.load(std::memory_order_relaxed)); }

    /// Get count by severity level.
    uint32_t getCountBySeverity(Severity sev) const {
        if (sev > FATAL) {
            return 0;
        }
        return severity_counts_[sev].load(std::memory_order_relaxed);
    }

    /// Clear the log (reset all counters and entries).
    void clear() {
        write_index_.store(0, std::memory_order_relaxed);
        total_count_.store(0, std::memory_order_relaxed);
        for (auto& severity_count : severity_counts_) {
            severity_count.store(0, std::memory_order_relaxed);
        }
        for (auto& entrie : entries_) {
            entrie = Entry{};
        }
    }

    /// Get the current write index (for testing).
    size_t getWriteIndex() const { return write_index_.load(std::memory_order_relaxed); }

private:
    ErrorLog() { clear(); }

    /// Get current timestamp in microseconds. Virtual-like via function pointer for testability.
    static uint64_t getTimestamp() { return static_cast<uint64_t>(esp_timer_get_time()); }

    Entry entries_[LOG_SIZE]{};
    std::atomic<size_t> write_index_{0};
    std::atomic<size_t> total_count_{0};
    std::atomic<uint32_t> severity_counts_[5] = {};

    static inline ErrorLog* test_instance_ = nullptr;

public:
    ErrorLog(const ErrorLog&) = delete;
    ErrorLog& operator=(const ErrorLog&) = delete;
};

}  // namespace chopper::safety
