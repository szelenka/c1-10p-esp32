#pragma once

#include <cstdint>
#include <cstddef>

namespace chopper::hal {

/**
 * Serial port interface for reading and writing bytes to a UART.
 *
 * Abstracts byte-level I/O so that drivers like MaestroServoDriver,
 * SabertoothMotorDriver, and MP3AudioDriver can be tested without
 * real hardware. The ESP-IDF implementation wraps uart_read/write_bytes().
 *
 * Read methods have default implementations returning "no data" so that
 * existing write-only users (MaestroServoDriver, etc.) need zero changes.
 */
class ISerialPort {
public:
    virtual ~ISerialPort() = default;

    /// Write bytes to the serial port. Returns number of bytes written.
    virtual size_t write(const uint8_t* data, size_t length) = 0;

    /// Convenience: write a single byte.
    size_t write(uint8_t byte) { return write(&byte, 1); }

    /// Returns the number of bytes available for reading.
    virtual int available() { return 0; }

    /// Read a single byte. Returns -1 if no data available.
    virtual int read() { return -1; }
};

}  // namespace chopper::hal
