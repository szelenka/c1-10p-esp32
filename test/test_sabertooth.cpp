// Host-side tests for SabertoothMotorDriver.
// Verifies the Packet Serial protocol encoding and the driver lifecycle.
//
// Compile:
//   g++ -std=c++20 -I test/mocks -I main/include \
//       test/test_sabertooth.cpp -o build/test/test_sabertooth -pthread

#include <cstdio>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

#include "chopper/hal/ISerialPort.h"
#include "chopper/hal/SabertoothMotorDriver.h"

// Mock serial port that records all bytes written
class MockSerialPort : public chopper::hal::ISerialPort {
public:
    std::vector<uint8_t> bytes;
    size_t max_write_len = SIZE_MAX;

    size_t write(const uint8_t* data, size_t length) override {
        const size_t writable = (max_write_len < length) ? max_write_len : length;
        for (size_t i = 0; i < writable; i++) {
            bytes.push_back(data[i]);
        }
        return writable;
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

// Helper to verify a 4-byte Packet Serial frame
static bool verifyPacket(const std::vector<uint8_t>& bytes, size_t offset,
                         uint8_t addr, uint8_t cmd, uint8_t value) {
    if (offset + 4 > bytes.size()) return false;
    uint8_t checksum = (addr + cmd + value) & 0x7F;
    return bytes[offset] == addr &&
           bytes[offset+1] == cmd &&
           bytes[offset+2] == value &&
           bytes[offset+3] == checksum;
}

static int legacyPowerForSet(float speed, bool inverted) {
    float target_speed = std::clamp(speed, -1.0f, 1.0f);
    if (inverted) {
        target_speed *= -1.0f;
    }
    int power = static_cast<int8_t>(target_speed * static_cast<int8_t>(127));
    if (power > chopper::hal::SabertoothMotorDriver::MAX_THROTTLE_POWER) {
        power = chopper::hal::SabertoothMotorDriver::MAX_THROTTLE_POWER;
    }
    if (power < -chopper::hal::SabertoothMotorDriver::MAX_THROTTLE_POWER) {
        power = -chopper::hal::SabertoothMotorDriver::MAX_THROTTLE_POWER;
    }
    return power;
}

static bool verifyLegacyMotorPacket(const std::vector<uint8_t>& bytes, size_t offset,
                                    uint8_t addr, uint8_t motor_id, float speed,
                                    bool inverted) {
    const int power = legacyPowerForSet(speed, inverted);
    const uint8_t cmd_base = (motor_id == 2) ? 4 : 0;
    const uint8_t cmd = static_cast<uint8_t>(cmd_base + ((power < 0) ? 1 : 0));
    const uint8_t value = static_cast<uint8_t>((power < 0) ? -power : power);
    return verifyPacket(bytes, offset, addr, cmd, value);
}

// ---- Tests ----

void test_init_can_opt_into_autobaud() {
    TEST(sabertooth_init_can_opt_into_autobaud);

    MockSerialPort serial;
    chopper::hal::SabertoothMotorDriver driver(serial, 128, 1, "test", true);

    driver.init();

    // init() should send the autobaud byte 0xAA
    ASSERT(serial.bytes.size() >= 1);
    ASSERT(serial.bytes[0] == 0xAA);

    PASS();
}

void test_init_skips_autobaud_by_default() {
    TEST(sabertooth_init_skips_autobaud_by_default);

    MockSerialPort serial;
    chopper::hal::SabertoothMotorDriver driver(serial, 128, 1, "test");

    auto status = driver.init();

    ASSERT(status == chopper::hal::DriverStatus::kReady);
    ASSERT(!driver.isAutobaudOnInitEnabled());
    ASSERT(serial.bytes.empty());

    PASS();
}

void test_shared_autobaud_helper_sends_byte() {
    TEST(sabertooth_shared_autobaud_helper_sends_byte);

    MockSerialPort serial;

    ASSERT(chopper::hal::SabertoothMotorDriver::sendSharedAutobaud(serial));
    ASSERT(serial.bytes.size() == 1);
    ASSERT(serial.bytes[0] == chopper::hal::SabertoothMotorDriver::AUTOBAUD_BYTE);

    PASS();
}

void test_init_can_skip_autobaud_for_shared_bus() {
    TEST(sabertooth_init_can_skip_autobaud_for_shared_bus);

    MockSerialPort serial;
    chopper::hal::SabertoothMotorDriver driver(serial, 128, 1, "test", false);

    auto status = driver.init();

    ASSERT(status == chopper::hal::DriverStatus::kReady);
    ASSERT(driver.getStatus() == chopper::hal::DriverStatus::kReady);
    ASSERT(!driver.isAutobaudOnInitEnabled());
    ASSERT(serial.bytes.empty());

    PASS();
}

void test_shared_bus_sends_single_autobaud() {
    TEST(sabertooth_shared_bus_sends_single_autobaud);

    MockSerialPort serial;
    chopper::hal::SabertoothMotorDriver left(serial, 129, 1, "left");
    chopper::hal::SabertoothMotorDriver right(serial, 129, 2, "right");
    chopper::hal::SabertoothMotorDriver dome(serial, 128, 1, "dome");

    ASSERT(chopper::hal::SabertoothMotorDriver::sendSharedAutobaud(serial));
    left.init();
    right.init();
    dome.init();

    ASSERT(serial.bytes.size() == 1);
    ASSERT(serial.bytes[0] == chopper::hal::SabertoothMotorDriver::AUTOBAUD_BYTE);

    PASS();
}

void test_init_lifecycle() {
    TEST(sabertooth_init_lifecycle);

    MockSerialPort serial;
    chopper::hal::SabertoothMotorDriver driver(serial, 128, 1, "test");

    ASSERT(driver.getStatus() == chopper::hal::DriverStatus::kUninitialized);

    auto status = driver.init();
    ASSERT(status == chopper::hal::DriverStatus::kReady);
    ASSERT(driver.getStatus() == chopper::hal::DriverStatus::kReady);

    driver.shutdown();
    ASSERT(driver.getStatus() == chopper::hal::DriverStatus::kDisabled);

    PASS();
}

void test_init_write_failure_sets_error() {
    TEST(sabertooth_init_write_failure_sets_error);

    MockSerialPort serial;
    serial.max_write_len = 0;
    chopper::hal::SabertoothMotorDriver driver(serial, 128, 1, "test", true);

    auto status = driver.init();

    ASSERT(status == chopper::hal::DriverStatus::kError);
    ASSERT(driver.getStatus() == chopper::hal::DriverStatus::kError);
    ASSERT(driver.getErrorState().code == chopper::hal::SabertoothMotorDriver::ERROR_SERIAL_WRITE);

    PASS();
}

void test_motor1_forward() {
    TEST(sabertooth_motor1_forward);

    MockSerialPort serial;
    chopper::hal::SabertoothMotorDriver driver(serial, 128, 1, "test");
    driver.init();

    serial.clear();
    driver.set(0.5f);
    driver.update();

    // Motor 1 forward: cmd=0, value=floor(0.5*127)=63
    // Packet: [128, 0, 63, checksum]
    ASSERT(serial.bytes.size() == 4);
    ASSERT(verifyPacket(serial.bytes, 0, 128, 0, 63));

    PASS();
}

void test_packet_write_failure_sets_error() {
    TEST(sabertooth_packet_write_failure_sets_error);

    MockSerialPort serial;
    chopper::hal::SabertoothMotorDriver driver(serial, 128, 1, "test");
    driver.init();

    serial.clear();
    serial.max_write_len = 2;
    driver.set(0.5f);
    driver.update();

    ASSERT(driver.getStatus() == chopper::hal::DriverStatus::kError);
    ASSERT(driver.getErrorState().code == chopper::hal::SabertoothMotorDriver::ERROR_SERIAL_WRITE);
    ASSERT(serial.bytes.size() == 2);

    PASS();
}

void test_motor1_reverse() {
    TEST(sabertooth_motor1_reverse);

    MockSerialPort serial;
    chopper::hal::SabertoothMotorDriver driver(serial, 128, 1, "test");
    driver.init();

    serial.clear();
    driver.set(-0.5f);
    driver.update();

    // Motor 1 reverse: cmd=1, value=63
    ASSERT(serial.bytes.size() == 4);
    ASSERT(verifyPacket(serial.bytes, 0, 128, 1, 63));

    PASS();
}

void test_motor2_forward() {
    TEST(sabertooth_motor2_forward);

    MockSerialPort serial;
    chopper::hal::SabertoothMotorDriver driver(serial, 129, 2, "test");
    driver.init();

    serial.clear();
    driver.set(1.0f);
    driver.update();

    // Motor 2 forward: cmd=4, value clamped to 126 by the legacy library
    ASSERT(serial.bytes.size() == 4);
    ASSERT(verifyPacket(serial.bytes, 0, 129, 4, 126));

    PASS();
}

void test_motor2_reverse() {
    TEST(sabertooth_motor2_reverse);

    MockSerialPort serial;
    chopper::hal::SabertoothMotorDriver driver(serial, 129, 2, "test");
    driver.init();

    serial.clear();
    driver.set(-1.0f);
    driver.update();

    // Motor 2 reverse: cmd=5, value clamped to 126 by the legacy library
    ASSERT(serial.bytes.size() == 4);
    ASSERT(verifyPacket(serial.bytes, 0, 129, 5, 126));

    PASS();
}

void test_checksum_calculation() {
    TEST(sabertooth_checksum_calculation);

    MockSerialPort serial;
    chopper::hal::SabertoothMotorDriver driver(serial, 130, 1, "test");
    driver.init();

    serial.clear();
    driver.set(0.3f);
    driver.update();

    // addr=130, cmd=0 (motor1 fwd), value=floor(0.3*127)=38
    // checksum = (130 + 0 + 38) & 0x7F = 168 & 0x7F = 40
    ASSERT(serial.bytes.size() == 4);
    ASSERT(serial.bytes[0] == 130);
    ASSERT(serial.bytes[1] == 0);
    ASSERT(serial.bytes[2] == 38);
    uint8_t expected_cksum = (130 + 0 + 38) & 0x7F;
    ASSERT(serial.bytes[3] == expected_cksum);

    PASS();
}

void test_power_clamping_to_126() {
    TEST(sabertooth_power_clamped_to_126);

    MockSerialPort serial;
    chopper::hal::SabertoothMotorDriver driver(serial, 128, 1, "test");
    driver.init();

    // Full forward
    serial.clear();
    driver.set(1.0f);
    driver.update();
    ASSERT(serial.bytes[2] == 126);  // value byte

    // Full reverse
    serial.clear();
    driver.set(-1.0f);
    driver.update();
    ASSERT(serial.bytes[2] == 126);  // value byte (absolute)

    PASS();
}

void test_legacy_packet_equivalence_table() {
    TEST(sabertooth_legacy_packet_equivalence_table);

    constexpr float speeds[] = {-1.25f, -1.0f, -0.8f, -0.5f, -0.25f, 0.0f, 0.25f, 0.5f, 0.8f, 1.0f, 1.25f};

    for (const bool inverted : {false, true}) {
        for (const uint8_t motor_id : {static_cast<uint8_t>(1), static_cast<uint8_t>(2)}) {
            MockSerialPort serial;
            chopper::hal::SabertoothMotorDriver driver(serial, 129, motor_id, "test");
            driver.init();
            driver.setInverted(inverted);

            for (const float speed : speeds) {
                serial.clear();
                driver.set(speed);
                driver.update();

                ASSERT(serial.bytes.size() == 4);
                ASSERT(verifyLegacyMotorPacket(serial.bytes, 0, 129, motor_id, speed, inverted));
            }
        }
    }

    PASS();
}

void test_zero_power() {
    TEST(sabertooth_zero_power);

    MockSerialPort serial;
    chopper::hal::SabertoothMotorDriver driver(serial, 128, 1, "test");
    driver.init();

    serial.clear();
    driver.set(0.0f);
    driver.update();

    // Zero power: motor1 forward cmd=0, value=0
    ASSERT(serial.bytes.size() == 4);
    ASSERT(verifyPacket(serial.bytes, 0, 128, 0, 0));

    PASS();
}

void test_inversion() {
    TEST(sabertooth_inversion);

    MockSerialPort serial;
    chopper::hal::SabertoothMotorDriver driver(serial, 128, 1, "test");
    driver.init();

    driver.setInverted(true);
    ASSERT(driver.isInverted());

    serial.clear();
    driver.set(0.5f);
    driver.update();

    // Inverted: 0.5 becomes -0.5 → motor1 reverse (cmd=1), value=63
    ASSERT(serial.bytes.size() == 4);
    ASSERT(verifyPacket(serial.bytes, 0, 128, 1, 63));

    PASS();
}

void test_stop_sends_zero() {
    TEST(sabertooth_stop_sends_zero);

    MockSerialPort serial;
    chopper::hal::SabertoothMotorDriver driver(serial, 128, 1, "test");
    driver.init();

    driver.set(0.8f);
    serial.clear();

    driver.stop();

    // stop() should set speed to 0 and send motor command with value=0
    ASSERT(driver.get() == 0.0f);
    ASSERT(serial.bytes.size() == 4);
    ASSERT(serial.bytes[2] == 0);

    PASS();
}

void test_disable_sends_zero() {
    TEST(sabertooth_disable_sends_zero);

    MockSerialPort serial;
    chopper::hal::SabertoothMotorDriver driver(serial, 128, 1, "test");
    driver.init();

    driver.set(0.8f);
    serial.clear();

    driver.disable();

    ASSERT(driver.get() == 0.0f);
    ASSERT(serial.bytes.size() == 4);
    ASSERT(serial.bytes[2] == 0);

    PASS();
}

void test_set_timeout() {
    TEST(sabertooth_set_timeout);

    MockSerialPort serial;
    chopper::hal::SabertoothMotorDriver driver(serial, 128, 1, "test");
    driver.init();

    serial.clear();
    driver.setTimeout(50);

    // cmd=14 (SET_TIMEOUT), value=50
    ASSERT(serial.bytes.size() == 4);
    ASSERT(verifyPacket(serial.bytes, 0, 128, 14, 50));

    PASS();
}

void test_set_ramping() {
    TEST(sabertooth_set_ramping);

    MockSerialPort serial;
    chopper::hal::SabertoothMotorDriver driver(serial, 128, 1, "test");
    driver.init();

    serial.clear();
    driver.setRamping(80);

    // cmd=16 (SET_RAMPING), value=80
    ASSERT(serial.bytes.size() == 4);
    ASSERT(verifyPacket(serial.bytes, 0, 128, 16, 80));

    PASS();
}

void test_set_deadband() {
    TEST(sabertooth_set_deadband);

    MockSerialPort serial;
    chopper::hal::SabertoothMotorDriver driver(serial, 128, 1, "test");
    driver.init();

    serial.clear();
    driver.setDeadband(10);

    // cmd=17 (SET_DEADBAND), value=10
    ASSERT(serial.bytes.size() == 4);
    ASSERT(verifyPacket(serial.bytes, 0, 128, 17, 10));

    PASS();
}

void test_update_skips_when_not_ready() {
    TEST(sabertooth_update_skips_when_not_ready);

    MockSerialPort serial;
    chopper::hal::SabertoothMotorDriver driver(serial, 128, 1, "test");
    // Don't call init()

    driver.set(0.5f);
    driver.update();

    // Only the autobaud byte should NOT have been sent (no init called)
    ASSERT(serial.bytes.empty());

    PASS();
}

void test_speed_clamping() {
    TEST(sabertooth_speed_clamping);

    MockSerialPort serial;
    chopper::hal::SabertoothMotorDriver driver(serial, 128, 1, "test");

    driver.set(5.0f);
    ASSERT(driver.get() == 1.0f);

    driver.set(-5.0f);
    ASSERT(driver.get() == -1.0f);

    PASS();
}

void test_reset_restores_ready() {
    TEST(sabertooth_reset_restores_ready);

    MockSerialPort serial;
    chopper::hal::SabertoothMotorDriver driver(serial, 128, 1, "test");
    driver.init();

    driver.set(0.5f);
    driver.shutdown();
    ASSERT(driver.getStatus() == chopper::hal::DriverStatus::kDisabled);

    auto status = driver.reset();
    ASSERT(status == chopper::hal::DriverStatus::kReady);
    ASSERT(driver.get() == 0.0f);

    PASS();
}

// ---- Main ----

int main() {
    printf("=== Sabertooth Motor Driver Tests ===\n\n");

    test_init_can_opt_into_autobaud();
    test_init_skips_autobaud_by_default();
    test_shared_autobaud_helper_sends_byte();
    test_init_can_skip_autobaud_for_shared_bus();
    test_shared_bus_sends_single_autobaud();
    test_init_lifecycle();
    test_init_write_failure_sets_error();
    test_motor1_forward();
    test_packet_write_failure_sets_error();
    test_motor1_reverse();
    test_motor2_forward();
    test_motor2_reverse();
    test_checksum_calculation();
    test_power_clamping_to_126();
    test_legacy_packet_equivalence_table();
    test_zero_power();
    test_inversion();
    test_stop_sends_zero();
    test_disable_sends_zero();
    test_set_timeout();
    test_set_ramping();
    test_set_deadband();
    test_update_skips_when_not_ready();
    test_speed_clamping();
    test_reset_restores_ready();

    printf("\n=== Results: %d/%d passed ===\n", pass_count, test_count);
    return (pass_count == test_count) ? 0 : 1;
}
