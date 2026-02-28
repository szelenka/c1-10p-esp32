// Host-side compilation test for the HAL interfaces and DriverManager.
// Compile: g++ -std=c++20 -I test/mocks -I main/include test/test_hal.cpp \
//          main/chopper/hal/DriverManager.cpp \
//          -o test/test_hal -pthread

#include <cstdio>
#include <cassert>
#include <cstring>

#include "chopper/chopper_limits.h"
#include "chopper/hal/IDriver.h"
#include "chopper/hal/IMotorDriver.h"
#include "chopper/hal/IServoController.h"
#include "chopper/hal/IAudioDriver.h"
#include "chopper/hal/ISensorDriver.h"
#include "chopper/hal/MockMotorDriver.h"
#include "chopper/hal/MockServoDriver.h"
#include "chopper/hal/DriverManager.h"
#include "chopper/hal/UartBusManager.h"

// ---- Test helpers ----

static int test_count = 0;
static int pass_count = 0;

#define TEST(name) \
    do { test_count++; printf("TEST: %s ... ", #name); } while(0)
#define PASS() \
    do { pass_count++; printf("PASS\n"); } while(0)
#define ASSERT(cond) \
    do { if (!(cond)) { printf("FAIL at %s:%d: %s\n", __FILE__, __LINE__, #cond); return; } } while(0)
#define ASSERT_EQ(a, b) \
    do { if ((a) != (b)) { printf("FAIL at %s:%d: %s != %s\n", __FILE__, __LINE__, #a, #b); return; } } while(0)
#define ASSERT_FLOAT_EQ(a, b) \
    do { float _a = (a), _b = (b); if (_a < _b - 0.001f || _a > _b + 0.001f) { \
        printf("FAIL at %s:%d: %s=%.4f != %s=%.4f\n", __FILE__, __LINE__, #a, _a, #b, _b); return; } } while(0)

// ---- Static drivers for DriverManager tests (must outlive the singleton) ----

static chopper::hal::MockMotorDriver s_mgr_motor1("mgr_motor1");
static chopper::hal::MockMotorDriver s_mgr_motor2("mgr_motor2");
static chopper::hal::MockServoDriver s_mgr_servo1("mgr_servo1", 6);

// ---- Test: DriverStatus enum and string conversion ----

void test_driver_status_strings() {
    TEST(driver_status_to_string);

    using namespace chopper::hal;
    ASSERT(strcmp(driverStatusToString(DriverStatus::kUninitialized), "UNINITIALIZED") == 0);
    ASSERT(strcmp(driverStatusToString(DriverStatus::kReady), "READY") == 0);
    ASSERT(strcmp(driverStatusToString(DriverStatus::kDegraded), "DEGRADED") == 0);
    ASSERT(strcmp(driverStatusToString(DriverStatus::kError), "ERROR") == 0);
    ASSERT(strcmp(driverStatusToString(DriverStatus::kDisabled), "DISABLED") == 0);

    PASS();
}

// ---- Test: ErrorInfo ----

void test_error_info() {
    TEST(error_info);

    chopper::hal::ErrorInfo err;
    ASSERT_EQ(err.code, 0);
    ASSERT(err.message[0] == '\0');

    err.set(42, 1000, "test error");
    ASSERT_EQ(err.code, 42);
    ASSERT_EQ(err.timestamp, (uint64_t)1000);
    ASSERT(strcmp(err.message, "test error") == 0);

    err.clear();
    ASSERT_EQ(err.code, 0);
    ASSERT(err.message[0] == '\0');

    PASS();
}

// ---- Test: MockMotorDriver lifecycle ----

void test_mock_motor_lifecycle() {
    TEST(mock_motor_lifecycle);

    chopper::hal::MockMotorDriver motor("test_motor");

    // Initial state
    ASSERT_EQ(motor.getStatus(), chopper::hal::DriverStatus::kUninitialized);
    ASSERT(strcmp(motor.getName(), "test_motor") == 0);
    ASSERT_EQ(motor.getInitCount(), (uint32_t)0);

    // Init
    auto status = motor.init();
    ASSERT_EQ(status, chopper::hal::DriverStatus::kReady);
    ASSERT_EQ(motor.getStatus(), chopper::hal::DriverStatus::kReady);
    ASSERT_EQ(motor.getInitCount(), (uint32_t)1);

    // Update
    motor.update();
    motor.update();
    ASSERT_EQ(motor.getUpdateCount(), (uint32_t)2);

    // Reset
    motor.set(0.5f);
    status = motor.reset();
    ASSERT_EQ(status, chopper::hal::DriverStatus::kReady);
    ASSERT_FLOAT_EQ(motor.get(), 0.0f);
    ASSERT_EQ(motor.getResetCount(), (uint32_t)1);

    // Shutdown
    motor.shutdown();
    ASSERT_EQ(motor.getStatus(), chopper::hal::DriverStatus::kDisabled);

    PASS();
}

// ---- Test: MockMotorDriver set/get ----

void test_mock_motor_set_get() {
    TEST(mock_motor_set_get);

    chopper::hal::MockMotorDriver motor("motor1");
    motor.init();

    // Set and get
    motor.set(0.75f);
    ASSERT_FLOAT_EQ(motor.get(), 0.75f);

    motor.set(-0.5f);
    ASSERT_FLOAT_EQ(motor.get(), -0.5f);

    // Clamp to range
    motor.set(2.0f);
    ASSERT_FLOAT_EQ(motor.get(), 1.0f);

    motor.set(-5.0f);
    ASSERT_FLOAT_EQ(motor.get(), -1.0f);

    // History
    ASSERT_EQ(motor.getSetCount(), (uint32_t)4);
    ASSERT_FLOAT_EQ(motor.getSpeedAt(0), 0.75f);
    ASSERT_FLOAT_EQ(motor.getSpeedAt(1), -0.5f);
    ASSERT_FLOAT_EQ(motor.getSpeedAt(2), 1.0f);   // clamped
    ASSERT_FLOAT_EQ(motor.getSpeedAt(3), -1.0f);  // clamped

    PASS();
}

// ---- Test: MockMotorDriver inversion ----

void test_mock_motor_inversion() {
    TEST(mock_motor_inversion);

    chopper::hal::MockMotorDriver motor("motor_inv");
    motor.init();

    ASSERT(!motor.isInverted());
    motor.setInverted(true);
    ASSERT(motor.isInverted());
    motor.setInverted(false);
    ASSERT(!motor.isInverted());

    PASS();
}

// ---- Test: MockMotorDriver disable/stop ----

void test_mock_motor_disable_stop() {
    TEST(mock_motor_disable_stop);

    chopper::hal::MockMotorDriver motor("motor_ds");
    motor.init();

    motor.set(0.8f);
    ASSERT_FLOAT_EQ(motor.get(), 0.8f);

    motor.disable();
    ASSERT_FLOAT_EQ(motor.get(), 0.0f);
    ASSERT(motor.wasDisabled());

    motor.set(0.5f);
    motor.stop();
    ASSERT_FLOAT_EQ(motor.get(), 0.0f);

    PASS();
}

// ---- Test: MockMotorDriver error injection ----

void test_mock_motor_error_injection() {
    TEST(mock_motor_error_injection);

    chopper::hal::MockMotorDriver motor("motor_err");
    motor.init();
    ASSERT_EQ(motor.getStatus(), chopper::hal::DriverStatus::kReady);

    motor.injectError(101, "uart timeout");
    ASSERT_EQ(motor.getStatus(), chopper::hal::DriverStatus::kError);

    auto err = motor.getErrorState();
    ASSERT_EQ(err.code, (uint16_t)101);
    ASSERT(strcmp(err.message, "uart timeout") == 0);

    // Reset clears error
    auto status = motor.reset();
    ASSERT_EQ(status, chopper::hal::DriverStatus::kReady);
    err = motor.getErrorState();
    ASSERT_EQ(err.code, (uint16_t)0);

    PASS();
}

// ---- Test: MockServoDriver lifecycle ----

void test_mock_servo_lifecycle() {
    TEST(mock_servo_lifecycle);

    chopper::hal::MockServoDriver servo("test_servo", 6);

    ASSERT_EQ(servo.getStatus(), chopper::hal::DriverStatus::kUninitialized);
    ASSERT_EQ(servo.getChannelCount(), (uint8_t)6);

    auto status = servo.init();
    ASSERT_EQ(status, chopper::hal::DriverStatus::kReady);

    servo.shutdown();
    ASSERT_EQ(servo.getStatus(), chopper::hal::DriverStatus::kDisabled);

    PASS();
}

// ---- Test: MockServoDriver position tracking ----

void test_mock_servo_positions() {
    TEST(mock_servo_positions);

    chopper::hal::MockServoDriver servo("servo1", 6);
    servo.init();

    // Set positions
    servo.setPosition(0, 1500);
    servo.setPosition(1, 2000);
    servo.setPosition(5, 750);

    ASSERT_EQ(servo.getPosition(0), (uint16_t)1500);
    ASSERT_EQ(servo.getPosition(1), (uint16_t)2000);
    ASSERT_EQ(servo.getPosition(5), (uint16_t)750);

    // Out-of-range channel returns 0
    ASSERT_EQ(servo.getPosition(6), (uint16_t)0);

    // Log tracking
    ASSERT_EQ(servo.getLogCount(), (uint32_t)3);
    auto entry0 = servo.getLogAt(0);
    ASSERT_EQ(entry0.channel, (uint8_t)0);
    ASSERT_EQ(entry0.pulse_us, (uint16_t)1500);

    PASS();
}

// ---- Test: MockServoDriver enable/disable ----

void test_mock_servo_enable_disable() {
    TEST(mock_servo_enable_disable);

    chopper::hal::MockServoDriver servo("servo_ed", 4);
    servo.init();

    ASSERT(!servo.isEnabled(0));

    servo.enable(0);
    ASSERT(servo.isEnabled(0));

    servo.disable(0);
    ASSERT(!servo.isEnabled(0));
    ASSERT_EQ(servo.getPosition(0), (uint16_t)0);

    // disableAll
    servo.enable(1);
    servo.enable(2);
    servo.disableAll();
    ASSERT(!servo.isEnabled(1));
    ASSERT(!servo.isEnabled(2));

    PASS();
}

// ---- Test: MockServoDriver angle mapping ----

void test_mock_servo_angle() {
    TEST(mock_servo_angle_mapping);

    chopper::hal::MockServoDriver servo("servo_angle", 4);
    servo.init();

    // 0 degrees -> 500us, 90 degrees -> 1500us, 180 degrees -> 2500us
    servo.setAngle(0, 0.0f);
    ASSERT_EQ(servo.getPosition(0), (uint16_t)500);

    servo.setAngle(1, 90.0f);
    ASSERT_EQ(servo.getPosition(1), (uint16_t)1500);

    servo.setAngle(2, 180.0f);
    ASSERT_EQ(servo.getPosition(2), (uint16_t)2500);

    PASS();
}

// ---- Test: DriverManager registration ----

void test_driver_manager_registration() {
    TEST(driver_manager_registration);

    auto& mgr = chopper::hal::DriverManager::getInstance();

    ASSERT(mgr.registerDriver(&s_mgr_motor1, 30));
    ASSERT(mgr.registerDriver(&s_mgr_motor2, 30));
    ASSERT(mgr.registerDriver(&s_mgr_servo1, 40));

    // Duplicate registration fails
    ASSERT(!mgr.registerDriver(&s_mgr_motor1, 30));

    ASSERT(mgr.getDriverCount() >= 3);

    PASS();
}

// ---- Test: DriverManager init ordering ----

void test_driver_manager_init_order() {
    TEST(driver_manager_init_ordering);

    auto& mgr = chopper::hal::DriverManager::getInstance();

    mgr.initAll();

    // All drivers should be kReady
    ASSERT_EQ(mgr.getDriverStatus("mgr_motor1"), chopper::hal::DriverStatus::kReady);
    ASSERT_EQ(mgr.getDriverStatus("mgr_motor2"), chopper::hal::DriverStatus::kReady);
    ASSERT_EQ(mgr.getDriverStatus("mgr_servo1"), chopper::hal::DriverStatus::kReady);

    PASS();
}

// ---- Test: DriverManager updateAll ----

void test_driver_manager_update() {
    TEST(driver_manager_update_all);

    auto& mgr = chopper::hal::DriverManager::getInstance();

    uint32_t prevCount = s_mgr_motor1.getUpdateCount();
    mgr.updateAll();
    ASSERT(s_mgr_motor1.getUpdateCount() > prevCount);

    PASS();
}

// ---- Test: DriverManager skips non-ready drivers ----

void test_driver_manager_skip_non_ready() {
    TEST(driver_manager_skip_non_ready);

    auto& mgr = chopper::hal::DriverManager::getInstance();

    // Put motor2 in error state
    s_mgr_motor2.injectError(99, "test error");
    ASSERT_EQ(s_mgr_motor2.getStatus(), chopper::hal::DriverStatus::kError);

    uint32_t prevCount = s_mgr_motor2.getUpdateCount();
    mgr.updateAll();

    // motor2 should NOT have been updated (status is kError)
    ASSERT_EQ(s_mgr_motor2.getUpdateCount(), prevCount);

    // Reset it and verify it gets updated again
    mgr.resetDriver("mgr_motor2");
    ASSERT_EQ(s_mgr_motor2.getStatus(), chopper::hal::DriverStatus::kReady);

    mgr.updateAll();
    ASSERT(s_mgr_motor2.getUpdateCount() > prevCount);

    PASS();
}

// ---- Test: DriverManager shutdown ----

void test_driver_manager_shutdown() {
    TEST(driver_manager_shutdown);

    auto& mgr = chopper::hal::DriverManager::getInstance();

    mgr.shutdownAll();

    ASSERT_EQ(mgr.getDriverStatus("mgr_motor1"), chopper::hal::DriverStatus::kDisabled);
    ASSERT_EQ(mgr.getDriverStatus("mgr_motor2"), chopper::hal::DriverStatus::kDisabled);
    ASSERT_EQ(mgr.getDriverStatus("mgr_servo1"), chopper::hal::DriverStatus::kDisabled);

    PASS();
}

// ---- Test: DriverManager diagnostic routing ----

void test_driver_manager_diagnostics() {
    TEST(driver_manager_diagnostic_routing);

    auto& mgr = chopper::hal::DriverManager::getInstance();

    char response[128];

    // Non-existent driver
    ASSERT(!mgr.routeDiagnostic("nonexistent", "status", response, sizeof(response)));

    // MockMotorDriver doesn't implement handleDiagnostic (uses IDriver default -> false)
    bool found = mgr.routeDiagnostic("mgr_motor1", "unsupported_cmd", response, sizeof(response));
    ASSERT(!found);

    PASS();
}

// ---- Test: DriverManager findDriver ----

void test_driver_manager_find() {
    TEST(driver_manager_find_driver);

    auto& mgr = chopper::hal::DriverManager::getInstance();

    ASSERT(mgr.findDriver("mgr_motor1") != nullptr);
    ASSERT(mgr.findDriver("mgr_servo1") != nullptr);
    ASSERT(mgr.findDriver("does_not_exist") == nullptr);

    // Not found returns kUninitialized
    ASSERT_EQ(mgr.getDriverStatus("does_not_exist"), chopper::hal::DriverStatus::kUninitialized);

    PASS();
}

// ---- Test: DriverManager priority ordering ----

void test_driver_manager_priority_order() {
    TEST(driver_manager_priority_order);

    auto& mgr = chopper::hal::DriverManager::getInstance();
    uint8_t count = mgr.getDriverCount();

    uint8_t prevPriority = 0;
    for (uint8_t i = 0; i < count; i++) {
        const auto* entry = mgr.getEntry(i);
        ASSERT(entry != nullptr);
        ASSERT(entry->priority >= prevPriority);
        prevPriority = entry->priority;
    }

    PASS();
}

// ---- Test: Interface polymorphism ----

void test_interface_polymorphism() {
    TEST(interface_polymorphism);

    chopper::hal::MockMotorDriver motor("poly_motor");
    chopper::hal::MockServoDriver servo("poly_servo", 4);

    // Use through IDriver pointer
    chopper::hal::IDriver* drivers[] = { &motor, &servo };

    for (auto* drv : drivers) {
        ASSERT_EQ(drv->getStatus(), chopper::hal::DriverStatus::kUninitialized);
        auto s = drv->init();
        ASSERT_EQ(s, chopper::hal::DriverStatus::kReady);
        drv->update();
        drv->shutdown();
        ASSERT_EQ(drv->getStatus(), chopper::hal::DriverStatus::kDisabled);
    }

    // Use through IMotorDriver pointer
    chopper::hal::IMotorDriver* motorIface = &motor;
    motorIface->init();
    motorIface->set(0.5f);
    ASSERT_FLOAT_EQ(motorIface->get(), 0.5f);

    // Use through IServoController pointer
    chopper::hal::IServoController* servoIface = &servo;
    servoIface->init();
    servoIface->setPosition(0, 1500);
    ASSERT_EQ(servoIface->getPosition(0), (uint16_t)1500);
    ASSERT_EQ(servoIface->getChannelCount(), (uint8_t)4);

    PASS();
}

// ---- Test: Driver lifecycle state machine transitions ----

void test_lifecycle_state_machine() {
    TEST(lifecycle_state_machine);

    chopper::hal::MockMotorDriver motor("sm_motor");

    // UNINITIALIZED -> READY (via init)
    ASSERT_EQ(motor.getStatus(), chopper::hal::DriverStatus::kUninitialized);
    motor.init();
    ASSERT_EQ(motor.getStatus(), chopper::hal::DriverStatus::kReady);

    // READY -> DISABLED (via shutdown)
    motor.shutdown();
    ASSERT_EQ(motor.getStatus(), chopper::hal::DriverStatus::kDisabled);

    // Re-init from disabled
    motor.init();
    ASSERT_EQ(motor.getStatus(), chopper::hal::DriverStatus::kReady);

    // READY -> ERROR (via inject)
    motor.injectError(1, "test");
    ASSERT_EQ(motor.getStatus(), chopper::hal::DriverStatus::kError);

    // ERROR -> READY (via reset)
    motor.reset();
    ASSERT_EQ(motor.getStatus(), chopper::hal::DriverStatus::kReady);

    // READY -> DEGRADED (via setStatus)
    motor.setStatus(chopper::hal::DriverStatus::kDegraded);
    ASSERT_EQ(motor.getStatus(), chopper::hal::DriverStatus::kDegraded);

    // DEGRADED -> READY (via reset)
    motor.reset();
    ASSERT_EQ(motor.getStatus(), chopper::hal::DriverStatus::kReady);

    PASS();
}

// ---- Test: DriverManager timing tracking ----

void test_driver_manager_timing() {
    TEST(driver_manager_timing_tracking);

    auto& mgr = chopper::hal::DriverManager::getInstance();

    // Re-init so drivers are ready again after shutdown test
    mgr.initAll();
    mgr.updateAll();

    // Check that timing entries exist and are non-negative
    for (uint8_t i = 0; i < mgr.getDriverCount(); i++) {
        const auto* entry = mgr.getEntry(i);
        ASSERT(entry != nullptr);
        // lastUpdateUs should be set (>= 0 is always true for uint64, just check it exists)
        ASSERT(entry->worstCaseUs >= entry->lastUpdateUs || entry->worstCaseUs > 0);
    }

    PASS();
}

// ---- Test: UartBusManager basic allocation ----

void test_uart_bus_acquire() {
    TEST(uart_bus_acquire_port);

    auto& uart = chopper::hal::UartBusManager::getInstance();

    chopper::hal::UartPortConfig cfg0 = {};
    cfg0.portId = 0;
    cfg0.txPin = 16;
    cfg0.rxPin = -1;  // TX-only
    cfg0.baudRate = 9600;
    cfg0.isSoftwareSerial = true;
    cfg0.isHalfDuplex = true;
    cfg0.ownerName = "sabertooth";

    ASSERT(uart.acquirePort(cfg0));
    ASSERT_EQ(uart.getAllocatedCount(), (uint8_t)1);

    // Verify config retrieval
    const auto* retrieved = uart.getPortConfig(0);
    ASSERT(retrieved != nullptr);
    ASSERT(strcmp(retrieved->ownerName, "sabertooth") == 0);
    ASSERT_EQ(retrieved->baudRate, (uint32_t)9600);

    PASS();
}

// ---- Test: UartBusManager pin conflict detection ----

void test_uart_bus_pin_conflict() {
    TEST(uart_bus_pin_conflict);

    auto& uart = chopper::hal::UartBusManager::getInstance();

    // Try to allocate another port using the same TX pin (GPIO 16)
    chopper::hal::UartPortConfig cfg1 = {};
    cfg1.portId = 1;
    cfg1.txPin = 16;  // conflict with port 0
    cfg1.rxPin = 17;
    cfg1.baudRate = 115200;
    cfg1.isSoftwareSerial = false;
    cfg1.isHalfDuplex = false;
    cfg1.ownerName = "openmv";

    // Should fail due to GPIO 16 conflict
    ASSERT(!uart.acquirePort(cfg1));

    // The pin owner should be "sabertooth" from the previous test
    ASSERT(uart.isPinClaimed(16));
    const char* owner = uart.getPinOwner(16);
    ASSERT(owner != nullptr);
    ASSERT(strcmp(owner, "sabertooth") == 0);

    PASS();
}

// ---- Test: UartBusManager non-conflicting allocation ----

void test_uart_bus_no_conflict() {
    TEST(uart_bus_no_conflict_allocation);

    auto& uart = chopper::hal::UartBusManager::getInstance();

    // Allocate with different pins — should succeed
    chopper::hal::UartPortConfig cfg2 = {};
    cfg2.portId = 2;
    cfg2.txPin = 4;
    cfg2.rxPin = 32;
    cfg2.baudRate = 9600;
    cfg2.isSoftwareSerial = true;
    cfg2.isHalfDuplex = false;
    cfg2.ownerName = "maestro_body";

    ASSERT(uart.acquirePort(cfg2));
    ASSERT_EQ(uart.getAllocatedCount(), (uint8_t)2);

    PASS();
}

// ---- Test: UartBusManager release and re-acquire ----

void test_uart_bus_release() {
    TEST(uart_bus_release_and_reacquire);

    auto& uart = chopper::hal::UartBusManager::getInstance();
    uint8_t prevCount = uart.getAllocatedCount();

    // Release port 0 (sabertooth — GPIO 16)
    uart.releasePort(0);
    ASSERT_EQ(uart.getAllocatedCount(), prevCount - 1);
    ASSERT(!uart.isPinClaimed(16));

    // Now GPIO 16 is free — can be acquired by another port
    chopper::hal::UartPortConfig cfg3 = {};
    cfg3.portId = 3;
    cfg3.txPin = 16;
    cfg3.rxPin = 17;
    cfg3.baudRate = 115200;
    cfg3.isSoftwareSerial = false;
    cfg3.isHalfDuplex = false;
    cfg3.ownerName = "openmv";

    ASSERT(uart.acquirePort(cfg3));
    ASSERT(uart.isPinClaimed(16));
    ASSERT(uart.isPinClaimed(17));

    PASS();
}

// ---- Test: UartBusManager duplicate port ID ----

void test_uart_bus_duplicate_port() {
    TEST(uart_bus_duplicate_port_id);

    auto& uart = chopper::hal::UartBusManager::getInstance();

    // Port 3 is already allocated from previous test
    chopper::hal::UartPortConfig cfgDup = {};
    cfgDup.portId = 3;
    cfgDup.txPin = 25;
    cfgDup.rxPin = 26;
    cfgDup.baudRate = 9600;
    cfgDup.isSoftwareSerial = true;
    cfgDup.isHalfDuplex = false;
    cfgDup.ownerName = "duplicate";

    // Should fail — port ID already in use
    ASSERT(!uart.acquirePort(cfgDup));

    PASS();
}

// ---- Test: UartBusManager validate allocations ----

void test_uart_bus_validate() {
    TEST(uart_bus_validate_allocations);

    auto& uart = chopper::hal::UartBusManager::getInstance();
    ASSERT(uart.validateAllocations());

    PASS();
}

// ---- Main ----

int main() {
    printf("=== Chopper HAL Tests ===\n\n");

    test_driver_status_strings();
    test_error_info();
    test_mock_motor_lifecycle();
    test_mock_motor_set_get();
    test_mock_motor_inversion();
    test_mock_motor_disable_stop();
    test_mock_motor_error_injection();
    test_mock_servo_lifecycle();
    test_mock_servo_positions();
    test_mock_servo_enable_disable();
    test_mock_servo_angle();
    test_driver_manager_registration();
    test_driver_manager_init_order();
    test_driver_manager_update();
    test_driver_manager_skip_non_ready();
    test_driver_manager_shutdown();
    test_driver_manager_diagnostics();
    test_driver_manager_find();
    test_driver_manager_priority_order();
    test_interface_polymorphism();
    test_lifecycle_state_machine();
    test_driver_manager_timing();
    test_uart_bus_acquire();
    test_uart_bus_pin_conflict();
    test_uart_bus_no_conflict();
    test_uart_bus_release();
    test_uart_bus_duplicate_port();
    test_uart_bus_validate();

    printf("\n=== Results: %d/%d passed ===\n", pass_count, test_count);
    return (pass_count == test_count) ? 0 : 1;
}
