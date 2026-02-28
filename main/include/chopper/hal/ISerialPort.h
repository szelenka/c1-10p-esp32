#pragma once

#include <cstdint>
#include <cstddef>

namespace chopper {
namespace hal {

/**
 * Minimal serial port interface for writing bytes to a UART.
 *
 * Abstracts the byte-level write operation so that drivers like
 * MaestroServoDriver can be tested without real hardware.
 * The ESP-IDF implementation wraps uart_write_bytes().
 */
class ISerialPort {
public:
    virtual ~ISerialPort() = default;

    /// Write bytes to the serial port. Returns number of bytes written.
    virtual size_t write(const uint8_t* data, size_t length) = 0;

    /// Convenience: write a single byte.
    size_t write(uint8_t byte) { return write(&byte, 1); }
};

} // namespace hal
} // namespace chopper
