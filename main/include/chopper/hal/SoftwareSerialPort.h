#pragma once

#ifdef ESP_PLATFORM

#include "chopper/hal/ISerialPort.h"
#include "chopper/hal/sw_serial.h"

namespace chopper {
namespace hal {

/**
 * Software (bit-banged) serial port for ESP32.
 *
 * Wraps the sw_serial library for GPIO-based UART emulation.
 * Useful when all hardware UARTs are occupied.
 *
 * Limitations:
 *   - Half-duplex (TX blocks RX interrupts during transmission)
 *   - Max recommended baud: 115200
 *   - Max burst length: ~256 bytes
 */
class SoftwareSerialPort : public ISerialPort {
public:
    static constexpr int kDefaultBufferSize = 512;
    static constexpr gpio_num_t kNoRxPin = static_cast<gpio_num_t>(-1);

    /**
     * @param txPin   GPIO for transmit.
     * @param rxPin   GPIO for receive.
     * @param baud    Baud rate.
     * @param bufSize Receive ring buffer size (default 512).
     */
    SoftwareSerialPort(gpio_num_t txPin, gpio_num_t rxPin, uint32_t baud, int bufSize = kDefaultBufferSize)
        : m_baud(baud) {
        m_sw = sw_new(txPin, rxPin, false, bufSize);
    }

    SoftwareSerialPort(gpio_num_t txPin, uint32_t baud, int bufSize = kDefaultBufferSize)
        : SoftwareSerialPort(txPin, kNoRxPin, baud, bufSize) {}

    /**
     * Initialize the software serial port. Call once before use.
     */
    [[nodiscard]] bool begin() {
        if (!m_sw) {
            m_ready = false;
            return false;
        }
        m_ready = (sw_open(m_sw, m_baud) == ESP_OK);
        return m_ready;
    }

    ~SoftwareSerialPort() override {
        if (m_sw) {
            if (m_ready) {
                (void)sw_stop(m_sw);
            }
            sw_del(m_sw);
            m_sw = nullptr;
        }
    }

    size_t write(const uint8_t* data, size_t length) override {
        if (!m_sw || !m_ready || data == nullptr)
            return 0;
        for (size_t i = 0; i < length; i++) {
            if (sw_write(m_sw, data[i]) != 1) {
                return i;
            }
        }
        return length;
    }

    int available() override {
        if (!m_sw || !m_ready)
            return 0;
        return sw_any(m_sw);
    }

    int read() override {
        if (!m_sw || !m_ready)
            return -1;
        return sw_read(m_sw);
    }

    [[nodiscard]] bool isReady() const { return m_ready; }

private:
    SwSerial* m_sw = nullptr;
    uint32_t m_baud;
    bool m_ready = false;
};

}  // namespace hal
}  // namespace chopper

#endif  // ESP_PLATFORM
