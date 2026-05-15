// Host-side tests for MaestroServoDriver.
// Verifies the Pololu compact serial protocol encoding and
// the driver lifecycle (init, setPosition, update, disable, etc.).
//
// Compile:
//   g++ -std=c++20 -I test/mocks -I main/include \
//       test/test_maestro.cpp -o test/test_maestro -pthread

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <vector>

#include "chopper/hal/ISerialPort.h"
#include "chopper/hal/MaestroServoDriver.h"

// Mock serial port that records all bytes written
class MockSerialPort : public chopper::hal::ISerialPort {
public:
    std::vector<uint8_t> bytes;
    bool short_write_next = false;

    size_t write(const uint8_t* data, size_t length) override {
        size_t written = length;
        if (short_write_next && length > 0) {
            written = length - 1;
            short_write_next = false;
        }
        for (size_t i = 0; i < written; i++) {
            bytes.push_back(data[i]);
        }
        return written;
    }

    void clear() { bytes.clear(); }
};

// ---- Test framework ----

static int test_count = 0;
static int pass_count = 0;

#define TEST(name) \
    do { test_count++; printf("TEST: %s ... ", #name); } while(0)
#define PASS() \
    do { pass_count++; printf("PASS\n"); } while(0)
#define ASSERT(cond) \
    do { if (!(cond)) { printf("FAIL at %s:%d: %s\n", __FILE__, __LINE__, #cond); return; } } while(0)

// ---- Tests ----

void test_init_lifecycle() {
    TEST(maestro_init_lifecycle);

    MockSerialPort serial;
    chopper::hal::MaestroServoDriver driver(serial, 6, "test_maestro");

    ASSERT(driver.getStatus() == chopper::hal::DriverStatus::kUninitialized);
    ASSERT(driver.getChannelCount() == 6);

    auto status = driver.init();
    ASSERT(status == chopper::hal::DriverStatus::kReady);
    ASSERT(driver.getStatus() == chopper::hal::DriverStatus::kReady);

    driver.shutdown();
    ASSERT(driver.getStatus() == chopper::hal::DriverStatus::kDisabled);

    PASS();
}

void test_set_position_stores_locally() {
    TEST(maestro_set_position_stores_locally);

    MockSerialPort serial;
    chopper::hal::MaestroServoDriver driver(serial, 6, "test");
    driver.init();

    driver.setPosition(0, 1500);
    ASSERT(driver.getPosition(0) == 1500);

    driver.setPosition(3, 2000);
    ASSERT(driver.getPosition(3) == 2000);

    // Out of range channel is ignored
    driver.setPosition(10, 999);
    ASSERT(driver.getPosition(10) == 0);

    // No bytes sent yet — positions are buffered until update()
    ASSERT(serial.bytes.empty());

    PASS();
}

void test_set_angle_mapping() {
    TEST(maestro_set_angle_maps_to_pulse);

    MockSerialPort serial;
    chopper::hal::MaestroServoDriver driver(serial, 6, "test");
    driver.init();

    // 0 degrees → 500us
    driver.setAngle(0, 0.0f);
    ASSERT(driver.getPosition(0) == 500);

    // 90 degrees → 1500us
    driver.setAngle(1, 90.0f);
    ASSERT(driver.getPosition(1) == 1500);

    // 180 degrees → 2500us
    driver.setAngle(2, 180.0f);
    ASSERT(driver.getPosition(2) == 2500);

    PASS();
}

void test_enable_disable() {
    TEST(maestro_enable_disable_channels);

    MockSerialPort serial;
    chopper::hal::MaestroServoDriver driver(serial, 6, "test");
    driver.init();

    ASSERT(!driver.isEnabled(0));

    driver.enable(0);
    ASSERT(driver.isEnabled(0));

    driver.disable(0);
    ASSERT(!driver.isEnabled(0));
    ASSERT(driver.getPosition(0) == 0);

    // disable() should send a setTarget(channel, 0) to hardware immediately
    // That's 4 bytes: 0x84, channel, 0x00, 0x00
    ASSERT(serial.bytes.size() == 4);
    ASSERT(serial.bytes[0] == 0x84);
    ASSERT(serial.bytes[1] == 0);
    ASSERT(serial.bytes[2] == 0);
    ASSERT(serial.bytes[3] == 0);

    PASS();
}

void test_update_sends_multi_target() {
    TEST(maestro_update_sends_multi_target_when_changed);

    MockSerialPort serial;
    chopper::hal::MaestroServoDriver driver(serial, 3, "test");
    driver.init();

    // Enable channels and set positions
    driver.enable(0);
    driver.enable(1);
    driver.enable(2);
    driver.setPosition(0, 1500);  // → 6000 quarter-us
    driver.setPosition(1, 1000);  // → 4000 quarter-us
    driver.setPosition(2, 2000);  // → 8000 quarter-us

    serial.clear();
    driver.update();

    // Should send multi-target command: 0x9F, count=3, firstCh=0, then 3 pairs
    // Total: 3 header + 3*2 data = 9 bytes
    ASSERT(serial.bytes.size() == 9);

    // Header
    ASSERT(serial.bytes[0] == 0x9F);  // CMD_SET_MULTI_TARGET
    ASSERT(serial.bytes[1] == 3);     // count
    ASSERT(serial.bytes[2] == 0);     // firstChannel

    // Channel 0: 1500us * 4 = 6000 = 0x1770
    // low 7 bits: 6000 & 0x7F = 0x70 (112)
    // high 7 bits: (6000 >> 7) & 0x7F = 46
    uint16_t target0 = 6000;
    ASSERT(serial.bytes[3] == (target0 & 0x7F));
    ASSERT(serial.bytes[4] == ((target0 >> 7) & 0x7F));

    // Channel 1: 1000us * 4 = 4000 = 0xFA0
    uint16_t target1 = 4000;
    ASSERT(serial.bytes[5] == (target1 & 0x7F));
    ASSERT(serial.bytes[6] == ((target1 >> 7) & 0x7F));

    // Channel 2: 2000us * 4 = 8000 = 0x1F40
    uint16_t target2 = 8000;
    ASSERT(serial.bytes[7] == (target2 & 0x7F));
    ASSERT(serial.bytes[8] == ((target2 >> 7) & 0x7F));

    PASS();
}

void test_update_skips_when_unchanged() {
    TEST(maestro_update_skips_when_targets_unchanged);

    MockSerialPort serial;
    chopper::hal::MaestroServoDriver driver(serial, 2, "test");
    driver.init();

    driver.enable(0);
    driver.setPosition(0, 1500);

    // First update sends
    driver.update();
    size_t first_count = serial.bytes.size();
    ASSERT(first_count > 0);

    // Second update with no changes — nothing sent
    serial.clear();
    driver.update();
    ASSERT(serial.bytes.empty());

    // Change a position — next update sends
    driver.setPosition(0, 1600);
    driver.update();
    ASSERT(!serial.bytes.empty());

    PASS();
}

void test_update_skips_when_not_ready() {
    TEST(maestro_update_skips_when_not_ready);

    MockSerialPort serial;
    chopper::hal::MaestroServoDriver driver(serial, 2, "test");
    // Don't call init() — status is kUninitialized

    driver.enable(0);
    driver.setPosition(0, 1500);
    driver.update();

    ASSERT(serial.bytes.empty());

    PASS();
}

void test_disabled_channel_sends_zero_target() {
    TEST(maestro_disabled_channel_sends_zero_target);

    MockSerialPort serial;
    chopper::hal::MaestroServoDriver driver(serial, 2, "test");
    driver.init();

    // Only enable channel 0
    driver.enable(0);
    driver.setPosition(0, 1500);
    driver.setPosition(1, 2000);  // channel 1 not enabled

    serial.clear();
    driver.update();

    // Channel 1 should have target=0 (disabled)
    // Header(3) + 2 channels * 2 bytes = 7 total
    ASSERT(serial.bytes.size() == 7);
    // Channel 1 target bytes (index 5,6) should be 0
    ASSERT(serial.bytes[5] == 0);
    ASSERT(serial.bytes[6] == 0);

    PASS();
}

void test_set_speed_protocol() {
    TEST(maestro_set_speed_sends_correct_bytes);

    MockSerialPort serial;
    chopper::hal::MaestroServoDriver driver(serial, 6, "test");
    driver.init();

    serial.clear();
    driver.setSpeed(2, 100);

    // 4 bytes: 0x87, channel, speed_low, speed_high
    ASSERT(serial.bytes.size() == 4);
    ASSERT(serial.bytes[0] == 0x87);
    ASSERT(serial.bytes[1] == 2);
    ASSERT(serial.bytes[2] == (100 & 0x7F));
    ASSERT(serial.bytes[3] == ((100 >> 7) & 0x7F));

    PASS();
}

void test_set_acceleration_protocol() {
    TEST(maestro_set_acceleration_sends_correct_bytes);

    MockSerialPort serial;
    chopper::hal::MaestroServoDriver driver(serial, 6, "test");
    driver.init();

    serial.clear();
    driver.setAcceleration(5, 200);

    // 4 bytes: 0x89, channel, accel_low, accel_high
    ASSERT(serial.bytes.size() == 4);
    ASSERT(serial.bytes[0] == 0x89);
    ASSERT(serial.bytes[1] == 5);
    ASSERT(serial.bytes[2] == (200 & 0x7F));
    ASSERT(serial.bytes[3] == ((200 >> 7) & 0x7F));

    PASS();
}

void test_disable_all() {
    TEST(maestro_disable_all_clears_state);

    MockSerialPort serial;
    chopper::hal::MaestroServoDriver driver(serial, 3, "test");
    driver.init();

    driver.enable(0);
    driver.enable(1);
    driver.enable(2);
    driver.setPosition(0, 1500);
    driver.setPosition(1, 1600);
    driver.setPosition(2, 1700);

    driver.disableAll();

    ASSERT(!driver.isEnabled(0));
    ASSERT(!driver.isEnabled(1));
    ASSERT(!driver.isEnabled(2));
    ASSERT(driver.getPosition(0) == 0);
    ASSERT(driver.getPosition(1) == 0);
    ASSERT(driver.getPosition(2) == 0);

    // disableAll() must send an immediate all-channel zero target because
    // shutdown/e-stop paths do not get another safe driver update.
    ASSERT(serial.bytes.size() == 9);
    ASSERT(serial.bytes[0] == 0x9F);
    ASSERT(serial.bytes[1] == 3);
    ASSERT(serial.bytes[2] == 0);
    for (size_t i = 3; i < serial.bytes.size(); i++) {
        ASSERT(serial.bytes[i] == 0);
    }

    PASS();
}

void test_update_write_failure_sets_error() {
    TEST(maestro_update_write_failure_sets_error);

    MockSerialPort serial;
    chopper::hal::MaestroServoDriver driver(serial, 2, "test");
    driver.init();

    driver.enable(0);
    driver.setPosition(0, 1500);
    serial.short_write_next = true;
    driver.update();

    ASSERT(driver.getStatus() == chopper::hal::DriverStatus::kError);
    ASSERT(driver.getErrorState().code == chopper::hal::MaestroServoDriver::ERROR_SERIAL_WRITE);

    PASS();
}

void test_disable_all_retries_after_error() {
    TEST(maestro_disable_all_retries_after_error);

    MockSerialPort serial;
    chopper::hal::MaestroServoDriver driver(serial, 2, "test");
    driver.init();

    driver.enable(0);
    driver.setPosition(0, 1500);
    serial.short_write_next = true;
    driver.update();
    ASSERT(driver.getStatus() == chopper::hal::DriverStatus::kError);

    serial.clear();
    driver.disableAll();

    ASSERT(serial.bytes.size() == 7);
    ASSERT(serial.bytes[0] == 0x9F);
    ASSERT(serial.bytes[1] == 2);
    ASSERT(serial.bytes[2] == 0);
    for (size_t i = 3; i < serial.bytes.size(); i++) {
        ASSERT(serial.bytes[i] == 0);
    }

    PASS();
}

void test_disable_write_failure_sets_error() {
    TEST(maestro_disable_write_failure_sets_error);

    MockSerialPort serial;
    chopper::hal::MaestroServoDriver driver(serial, 2, "test");
    driver.init();

    driver.enable(0);
    serial.short_write_next = true;
    driver.disable(0);

    ASSERT(driver.getStatus() == chopper::hal::DriverStatus::kError);
    ASSERT(driver.getErrorState().code == chopper::hal::MaestroServoDriver::ERROR_SERIAL_WRITE);

    PASS();
}

void test_reset_restores_ready() {
    TEST(maestro_reset_restores_ready);

    MockSerialPort serial;
    chopper::hal::MaestroServoDriver driver(serial, 3, "test");
    driver.init();

    driver.enable(0);
    driver.setPosition(0, 1500);

    driver.shutdown();
    ASSERT(driver.getStatus() == chopper::hal::DriverStatus::kDisabled);

    auto status = driver.reset();
    ASSERT(status == chopper::hal::DriverStatus::kReady);
    ASSERT(!driver.isEnabled(0));
    ASSERT(driver.getPosition(0) == 0);

    PASS();
}

void test_diagnostic_status() {
    TEST(maestro_diagnostic_status_command);

    MockSerialPort serial;
    chopper::hal::MaestroServoDriver driver(serial, 6, "test_diag");
    driver.init();

    char response[128] = {0};
    ASSERT(driver.handleDiagnostic("status", response, sizeof(response)));
    // Should contain "READY" and "6 channels"
    ASSERT(strstr(response, "READY") != nullptr);
    ASSERT(strstr(response, "6 channels") != nullptr);

    PASS();
}

void test_diagnostic_home() {
    TEST(maestro_diagnostic_home_command);

    MockSerialPort serial;
    chopper::hal::MaestroServoDriver driver(serial, 3, "test");
    driver.init();

    driver.enable(0);
    driver.setPosition(0, 1500);

    char response[128] = {0};
    ASSERT(driver.handleDiagnostic("home", response, sizeof(response)));
    ASSERT(!driver.isEnabled(0));
    ASSERT(driver.getPosition(0) == 0);

    PASS();
}

void test_large_target_encoding() {
    TEST(maestro_large_target_7bit_encoding);

    MockSerialPort serial;
    chopper::hal::MaestroServoDriver driver(serial, 1, "test");
    driver.init();

    // 2500us * 4 = 10000 quarter-us
    // Binary: 10011100010000
    // low 7 bits: 0010000 = 16
    // high 7 bits: 1001110 = 78
    driver.enable(0);
    driver.setPosition(0, 2500);

    serial.clear();
    driver.update();

    // Header(3) + 1 channel * 2 = 5 bytes
    ASSERT(serial.bytes.size() == 5);
    uint16_t target = 10000;
    ASSERT(serial.bytes[3] == (target & 0x7F));
    ASSERT(serial.bytes[4] == ((target >> 7) & 0x7F));

    PASS();
}

void test_channel_count_clamped() {
    TEST(maestro_channel_count_clamped_to_max);

    MockSerialPort serial;
    chopper::hal::MaestroServoDriver driver(serial, 50, "test");  // > kMaxChannels

    ASSERT(driver.getChannelCount() == 24);

    PASS();
}

// ---- Main ----

int main() {
    printf("=== Maestro Servo Driver Tests ===\n\n");

    test_init_lifecycle();
    test_set_position_stores_locally();
    test_set_angle_mapping();
    test_enable_disable();
    test_update_sends_multi_target();
    test_update_skips_when_unchanged();
    test_update_skips_when_not_ready();
    test_disabled_channel_sends_zero_target();
    test_set_speed_protocol();
    test_set_acceleration_protocol();
    test_disable_all();
    test_update_write_failure_sets_error();
    test_disable_all_retries_after_error();
    test_disable_write_failure_sets_error();
    test_reset_restores_ready();
    test_diagnostic_status();
    test_diagnostic_home();
    test_large_target_encoding();
    test_channel_count_clamped();

    printf("\n=== Results: %d/%d passed ===\n", pass_count, test_count);
    return (pass_count == test_count) ? 0 : 1;
}
