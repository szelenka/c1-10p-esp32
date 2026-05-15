#pragma once

#include "chopper/hal/IAudioDriver.h"
#include "chopper/hal/ISerialPort.h"
#include "esp_log.h"
#include <cstring>

#ifdef ESP_PLATFORM
#include "esp_random.h"
#endif

namespace chopper::hal {

/**
 * HAL audio driver for the SparkFun MP3 Trigger board.
 *
 * Implements the MP3 Trigger serial protocol directly via ISerialPort,
 * removing the Arduino MP3Trigger library dependency.
 *
 * Protocol reference:
 *   - Trigger track: ['t', trackNumber]
 *   - Set volume:    ['v', volume]    (0=loudest, 255=off on some FW)
 *   - Stop:          'O'
 *
 * Status responses from the board:
 *   - 'X' — track finished playing
 *   - 'E' — error
 *   - 'M' — trigger input event (followed by 3 bytes)
 *
 * Random sound selection is done from a fixed-size table of track numbers
 * configured at construction time.
 */
class MP3AudioDriver : public IAudioDriver {
public:
    static constexpr uint8_t kMaxRandomTracks = 32;

    /**
     * @param serial    Serial port for MP3 Trigger communication.
     * @param name      Driver name for diagnostics.
     */
    MP3AudioDriver(ISerialPort& serial, const char* name) : m_serial(&serial), m_name(name) {
        memset(m_randomTracks, 0, sizeof(m_randomTracks));
    }

    /**
     * Add a track number to the random sound pool.
     * Call before init(). Returns false if pool is full.
     */
    bool addRandomTrack(uint8_t track) {
        if (m_randomTrackCount >= kMaxRandomTracks) {
            return false;
        }
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
        // Poll the serial port for status responses from the MP3 Trigger
        mp3Update();
    }

    [[nodiscard]] DriverStatus getStatus() const override { return m_status; }
    [[nodiscard]] ErrorInfo getErrorState() const override { return m_lastError; }
    [[nodiscard]] const char* getName() const override { return m_name; }

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
        if (m_status != DriverStatus::kReady && m_status != DriverStatus::kDegraded) {
            return;
        }
        ESP_LOGD(m_name, "trigger track %d", track);
        mp3Trigger(track);
        m_playing = true;
    }

    void triggerRandom() override {
        if (m_randomTrackCount == 0) {
            return;
        }
        uint8_t index = randomIndex() % m_randomTrackCount;
        trigger(m_randomTracks[index]);
    }

    void setVolume(uint8_t volume) override {
        m_volume = volume;
        mp3SetVolume(volume);
    }

    [[nodiscard]] uint8_t getVolume() const override { return m_volume; }

    [[nodiscard]] bool isPlaying() const override { return m_playing; }

    void stop() override {
        mp3Stop();
        m_playing = false;
    }

    bool handleDiagnostic(const char* command, char* response, size_t maxLen) override {
        if ((command == nullptr) || (response == nullptr) || maxLen == 0) {
            return false;
        }

        if (strcmp(command, "status") == 0) {
            snprintf(response, maxLen, "%s, volume=%d, playing=%s", driverStatusToString(m_status), m_volume,
                     m_playing ? "true" : "false");
            return true;
        }
        return false;
    }

private:
    /**
     * Poll serial port for status bytes from the MP3 Trigger.
     *   'X' — track ended, clear playing state
     *   'E' — error, clear playing state
     *   'M' — trigger input event, consume 3 following bytes (best-effort)
     */
    void mp3Update() {
        while (m_serial->available() > 0) {
            int byte = m_serial->read();
            if (byte < 0) {
                break;
            }

            switch (static_cast<uint8_t>(byte)) {  // NOLINT(bugprone-branch-clone)
                case 'X':                          // Track finished
                    m_playing = false;
                    break;
                case 'x':
                    if (!m_playing) {
                        m_playing = false;
                    }
                    break;
                case 'E':  // Error
                    m_playing = false;
                    break;
                case 'M':  // Trigger input event — consume 3 following bytes
                    for (int i = 0; i < 3; i++) {
                        if (m_serial->available() > 0) {
                            m_serial->read();
                        }
                    }
                    break;
                default:
                    break;
            }
        }
    }

    void mp3Trigger(uint8_t track) {
        uint8_t buf[2];
        buf[0] = 't';
        buf[1] = track;
        m_serial->write(buf, 2);
    }

    void mp3SetVolume(uint8_t volume) {
        uint8_t buf[2];
        buf[0] = 'v';
        buf[1] = volume;
        m_serial->write(buf, 2);
    }

    void mp3Stop() {
        if (!m_playing) {
            return;
        }
        uint8_t byte = 'O';
        m_serial->write(&byte, 1);
    }

    uint32_t randomIndex() {
#ifdef ESP_PLATFORM
        return esp_random();
#else
        // Simple LCG for host-side testing (deterministic, not cryptographic)
        m_rngState = m_rngState * 1103515245 + 12345;
        return (m_rngState >> 16) & 0x7FFF;
#endif
    }

    ISerialPort* m_serial;
    const char* m_name;
    DriverStatus m_status = DriverStatus::kUninitialized;
    ErrorInfo m_lastError;
    uint8_t m_volume = 0;
    bool m_playing = false;

    uint8_t m_randomTracks[kMaxRandomTracks]{};
    uint8_t m_randomTrackCount = 0;

#ifndef ESP_PLATFORM
    uint32_t m_rngState = 42;  // LCG seed for host tests
#endif
};

}  // namespace chopper::hal
