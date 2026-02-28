#pragma once

#ifdef ESP_PLATFORM

#include "chopper/hal/ISerialPort.h"
#include "driver/uart.h"

namespace chopper {
namespace hal {

/**
 * ESP-IDF native hardware UART serial port.
 *
 * Wraps the ESP-IDF UART driver (uart_write_bytes, uart_read_bytes,
 * uart_get_buffered_data_len) for use with Sabertooth, Maestro,
 * MP3 Trigger, and other UART peripherals.
 */
class HardwareSerialPort : public ISerialPort {
public:
    static constexpr size_t kDefaultRxBufSize = 256;
    static constexpr size_t kDefaultTxBufSize = 0;  // No TX ring buffer (blocking writes)

    /**
     * @param port    UART port number (UART_NUM_0, UART_NUM_1, UART_NUM_2).
     * @param txPin   GPIO pin for TX.
     * @param rxPin   GPIO pin for RX.
     * @param baud    Baud rate.
     * @param rxBuf   Receive buffer size (default 256).
     */
    HardwareSerialPort(uart_port_t port, int txPin, int rxPin,
                       uint32_t baud, size_t rxBuf = kDefaultRxBufSize)
        : m_port(port)
        , m_txPin(txPin)
        , m_rxPin(rxPin)
        , m_baud(baud)
        , m_rxBufSize(rxBuf)
    {
    }

    /**
     * Initialize the UART peripheral. Call once before use.
     * Separated from constructor to allow deferred initialization.
     */
    void begin() {
        uart_config_t config = {};
        config.baud_rate = static_cast<int>(m_baud);
        config.data_bits = UART_DATA_8_BITS;
        config.parity    = UART_PARITY_DISABLE;
        config.stop_bits  = UART_STOP_BITS_1;
        config.flow_ctrl  = UART_HW_FLOWCTRL_DISABLE;
        config.source_clk = UART_SCLK_DEFAULT;

        uart_param_config(m_port, &config);
        uart_set_pin(m_port, m_txPin, m_rxPin,
                     UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
        uart_driver_install(m_port, m_rxBufSize, kDefaultTxBufSize, 0, nullptr, 0);
    }

    ~HardwareSerialPort() override {
        uart_driver_delete(m_port);
    }

    size_t write(const uint8_t* data, size_t length) override {
        int written = uart_write_bytes(m_port, data, length);
        return (written >= 0) ? static_cast<size_t>(written) : 0;
    }

    int available() override {
        size_t buffered = 0;
        uart_get_buffered_data_len(m_port, &buffered);
        return static_cast<int>(buffered);
    }

    int read() override {
        uint8_t byte;
        int len = uart_read_bytes(m_port, &byte, 1, 0);  // non-blocking (0 tick timeout)
        return (len == 1) ? byte : -1;
    }

private:
    uart_port_t m_port;
    int m_txPin;
    int m_rxPin;
    uint32_t m_baud;
    size_t m_rxBufSize;
};

} // namespace hal
} // namespace chopper

#endif // ESP_PLATFORM
