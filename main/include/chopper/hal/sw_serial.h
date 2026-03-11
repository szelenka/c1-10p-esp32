/**
 * ESP-IDF Software Serial (bit-banged UART)
 *
 * Ported from: https://github.com/junhuanchen/esp-idf-software-serial
 * Updated for ESP-IDF 5.x compatibility.
 *
 * SPDX-License-Identifier: MIT
 *
 * This is a header-only implementation. Include in exactly ONE translation
 * unit, or guard with an include-once mechanism at the build level.
 *
 * Limitations:
 *   - Half-duplex: TX critical section blocks RX interrupts
 *   - Max recommended baud: 115200
 *   - Max burst: ~256 bytes
 */

#pragma once

#ifdef ESP_PLATFORM

#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_cpu.h"
#include "esp_private/esp_clk.h"
#include "esp_rom_gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SW_EOF (-1)

typedef struct sw_serial {
    gpio_num_t rxPin, txPin;
    uint32_t buffSize, bitTime, rx_start_time, rx_end_time;
    bool invert, overflow;
    volatile uint32_t inPos, outPos;
    uint8_t* buffer;
} SwSerial;

// Forward declarations of internal helpers
static void IRAM_ATTR sw_rx_handler(void* arg);
static inline void sw_wait(uint32_t start, uint32_t ticks);

static inline SwSerial* sw_new(gpio_num_t tx, gpio_num_t rx, bool inverse, int buffSize) {
    SwSerial* self = (SwSerial*)malloc(sizeof(SwSerial));
    if (!self)
        return NULL;

    self->txPin = tx;
    self->rxPin = rx;
    self->invert = inverse;
    self->overflow = false;
    self->inPos = 0;
    self->outPos = 0;
    self->buffSize = (uint32_t)buffSize;
    self->buffer = (uint8_t*)malloc(buffSize);
    if (!self->buffer) {
        free(self);
        return NULL;
    }

    // Configure TX pin
    esp_rom_gpio_pad_select_gpio(tx);
    gpio_set_direction(tx, GPIO_MODE_OUTPUT);
    gpio_set_level(tx, inverse ? 0 : 1);

    // Configure RX pin
    esp_rom_gpio_pad_select_gpio(rx);
    gpio_set_direction(rx, GPIO_MODE_INPUT);
    gpio_set_pull_mode(rx, GPIO_PULLUP_ONLY);

    vTaskDelay(1);
    return self;
}

static inline void sw_del(SwSerial* self) {
    if (self) {
        free(self->buffer);
        free(self);
    }
}

static inline esp_err_t sw_enableRx(SwSerial* self, bool state) {
    if (state) {
        gpio_set_intr_type(self->rxPin, self->invert ? GPIO_INTR_POSEDGE : GPIO_INTR_NEGEDGE);
        esp_err_t err = gpio_install_isr_service(0);
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE)
            return err;
        return gpio_isr_handler_add(self->rxPin, sw_rx_handler, self);
    } else {
        gpio_isr_handler_remove(self->rxPin);
        gpio_uninstall_isr_service();
        return ESP_OK;
    }
}

static inline esp_err_t sw_open(SwSerial* self, uint32_t baudRate) {
    self->bitTime = esp_clk_cpu_freq() / baudRate;
    self->rx_start_time = self->bitTime + self->bitTime / 2;
    self->rx_end_time = self->bitTime * 9;

    return sw_enableRx(self, true);
}

static inline esp_err_t sw_stop(SwSerial* self) {
    return sw_enableRx(self, false);
}

static inline int sw_write(SwSerial* self, uint8_t byte) {
    if (self->invert)
        byte = ~byte;

    portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
    portENTER_CRITICAL(&mux);

    // Start bit
    gpio_set_level(self->txPin, self->invert ? 1 : 0);
    uint32_t start = esp_cpu_get_cycle_count();
    sw_wait(start, self->bitTime);

    // 8 data bits, LSB first
    for (int i = 0; i < 8; i++) {
        gpio_set_level(self->txPin, (byte & 1) ? 1 : 0);
        start = esp_cpu_get_cycle_count();
        sw_wait(start, self->bitTime);
        byte >>= 1;
    }

    // Stop bit
    gpio_set_level(self->txPin, self->invert ? 0 : 1);
    start = esp_cpu_get_cycle_count();
    sw_wait(start, self->bitTime);

    portEXIT_CRITICAL(&mux);
    return 1;
}

static inline int sw_read(SwSerial* self) {
    if (self->inPos == self->outPos)
        return SW_EOF;
    uint8_t byte = self->buffer[self->outPos];
    self->outPos = (self->outPos + 1) % self->buffSize;
    return byte;
}

static inline int sw_any(SwSerial* self) {
    int avail = (int)(self->inPos - self->outPos);
    if (avail < 0)
        avail += (int)self->buffSize;
    return avail;
}

static inline void sw_flush(SwSerial* self) {
    self->inPos = 0;
    self->outPos = 0;
    self->overflow = false;
}

static inline bool sw_overflow(SwSerial* self) {
    return self->overflow;
}

static inline int sw_peek(SwSerial* self) {
    if (self->inPos == self->outPos)
        return SW_EOF;
    return self->buffer[self->outPos];
}

// --- Internal helpers ---

static inline void sw_wait(uint32_t start, uint32_t ticks) {
    while ((esp_cpu_get_cycle_count() - start) < ticks) {
        // busy-wait
    }
}

static void IRAM_ATTR sw_rx_handler(void* arg) {
    SwSerial* self = (SwSerial*)arg;

    uint32_t start = esp_cpu_get_cycle_count();

    // Wait for start bit midpoint
    sw_wait(start, self->rx_start_time);

    uint8_t byte = 0;
    for (int i = 0; i < 8; i++) {
        byte >>= 1;
        if (gpio_get_level(self->rxPin)) {
            byte |= 0x80;
        }
        sw_wait(start, self->rx_start_time + self->bitTime * (i + 1));
    }

    if (self->invert)
        byte = ~byte;

    // Store in circular buffer
    uint32_t next = (self->inPos + 1) % self->buffSize;
    if (next != self->outPos) {
        self->buffer[self->inPos] = byte;
        self->inPos = next;
    } else {
        self->overflow = true;
    }
}

#ifdef __cplusplus
}
#endif

#endif  // ESP_PLATFORM
