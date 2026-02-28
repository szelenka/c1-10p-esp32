#pragma once

#include "chopper/hal/IAudioDriver.h"
#include "esp_log.h"
#include <cstring>

// Forward-declare to avoid pulling in the full MP3Trigger header.
class MP3Trigger;

namespace chopper {
namespace hal {

/**
 * HAL audio driver that wraps the SparkFun MP3Trigger library.
 *
 * Delegates to MP3Trigger for UART communication with the MP3 Trigger
 * board. Calls MP3Trigger::update() each cycle to handle serial I/O.
 *
 * Random sound selection is done from a fixed-size table of track numbers
 * configured at construction time.
 */
class MP3AudioDriver : public IAudioDriver {
public:
    static constexpr uint8_t kMaxRandomTracks = 32;

    /**
     * @param mp3       Reference to the MP3Trigger library object.
     * @param name      Driver name for diagnostics.
     */
    MP3AudioDriver(MP3Trigger& mp3, const char* name)
        : m_mp3(&mp3)
        , m_name(name)
    {
        memset(m_randomTracks, 0, sizeof(m_randomTracks));
    }

    /**
     * Add a track number to the random sound pool.
     * Call before init(). Returns false if pool is full.
     */
    bool addRandomTrack(uint8_t track) {
        if (m_randomTrackCount >= kMaxRandomTracks) return false;
        m_randomTracks[m_randomTrackCount++] = track;
        return true;
    }

    // -- IDriver interface --

    DriverStatus init() override {
        ESP_LOGI(m_name, "init audio driver");
        m_status = DriverStatus::kReady;
        return m_status;
    }

    void update() override {
        if (m_status != DriverStatus::kReady && m_status != DriverStatus::kDegraded) {
            return;
        }
        // MP3Trigger::update() polls the serial port for status responses
        mp3Update();
    }

    DriverStatus getStatus() const override { return m_status; }
    ErrorInfo getErrorState() const override { return m_lastError; }
    const char* getName() const override { return m_name; }

    DriverStatus reset() override {
        m_volume = 0;
        m_playing = false;
        m_lastError.clear();
        m_status = DriverStatus::kReady;
        return m_status;
    }

    void shutdown() override {
        stop();
        m_status = DriverStatus::kDisabled;
    }

    // -- IAudioDriver interface --

    void trigger(uint8_t track) override {
        if (m_status != DriverStatus::kReady && m_status != DriverStatus::kDegraded) return;
        ESP_LOGD(m_name, "trigger track %d", track);
        mp3Trigger(track);
        m_playing = true;
    }

    void triggerRandom() override {
        if (m_randomTrackCount == 0) return;
        // Use esp_random() on ESP32, or fallback to simple modular arithmetic
        uint8_t index = randomIndex() % m_randomTrackCount;
        trigger(m_randomTracks[index]);
    }

    void setVolume(uint8_t volume) override {
        m_volume = volume;
        mp3SetVolume(volume);
    }

    uint8_t getVolume() const override { return m_volume; }

    bool isPlaying() const override { return m_playing; }

    void stop() override {
        mp3Stop();
        m_playing = false;
    }

    bool handleDiagnostic(const char* command, char* response, size_t maxLen) override {
        if (!command || !response || maxLen == 0) return false;

        if (strcmp(command, "status") == 0) {
            snprintf(response, maxLen, "%s, volume=%d, playing=%s",
                     driverStatusToString(m_status), m_volume,
                     m_playing ? "true" : "false");
            return true;
        }
        return false;
    }

private:
    // Hardware abstraction points — separated for testability.
    // Actual implementations call through to the MP3Trigger library.
    void mp3Update();
    void mp3Trigger(uint8_t track);
    void mp3SetVolume(uint8_t volume);
    void mp3Stop();
    uint32_t randomIndex();

    MP3Trigger* m_mp3;
    const char* m_name;
    DriverStatus m_status = DriverStatus::kUninitialized;
    ErrorInfo m_lastError;
    uint8_t m_volume = 0;
    bool m_playing = false;

    uint8_t m_randomTracks[kMaxRandomTracks];
    uint8_t m_randomTrackCount = 0;
};

} // namespace hal
} // namespace chopper
