// Host-side tests for the safety system.
// Compile: g++ -std=c++20 -I test/mocks -I main/include test/test_safety.cpp \
//          main/chopper/safety/DegradationManager.cpp \
//          main/chopper/safety/EmergencyStopChain.cpp \
//          main/chopper/safety/SafetyManager.cpp \
//          -o test/test_safety -pthread

#include <cstdio>
#include <cassert>
#include <cstring>
#include <thread>
#include <vector>

#include "chopper/chopper_limits.h"
#include "chopper/safety/ErrorLog.h"
#include "chopper/safety/MotorSafetyMonitor.h"
#include "chopper/safety/DegradationManager.h"
#include "chopper/safety/EmergencyStopChain.h"
#include "chopper/safety/SafetyManager.h"

// ---- Test helpers ----

static int test_count = 0;
static int pass_count = 0;

#define TEST(name) \
    do { test_count++; printf("TEST: %s ... ", #name); } while(0)
#define PASS() \
    do { pass_count++; printf("PASS\n"); } while(0)
#define ASSERT(cond) \
    do { if (!(cond)) { printf("FAIL at %s:%d: %s\n", __FILE__, __LINE__, #cond); return; } } while(0)

// ---- ErrorLog Tests ----

void test_error_log_basic() {
    TEST(error_log_basic);

    auto& log = chopper::safety::ErrorLog::getInstance();
    log.clear();

    ASSERT(log.getTotalCount() == 0);

    log.log(chopper::safety::ErrorLog::MOTOR_TIMEOUT, 1, 5000);
    ASSERT(log.getTotalCount() == 1);

    log.log(chopper::safety::ErrorLog::TIMING_WARNING, 2, 800,
            chopper::safety::ErrorLog::WARNING);
    ASSERT(log.getTotalCount() == 2);

    // Read back
    chopper::safety::ErrorLog::Entry entries[2];
    size_t count = log.readRecent(entries, 2);
    ASSERT(count == 2);

    // Most recent first
    ASSERT(entries[0].category == chopper::safety::ErrorLog::TIMING_WARNING);
    ASSERT(entries[0].source_id == 2);
    ASSERT(entries[0].detail == 800);
    ASSERT(entries[0].severity == chopper::safety::ErrorLog::WARNING);

    ASSERT(entries[1].category == chopper::safety::ErrorLog::MOTOR_TIMEOUT);
    ASSERT(entries[1].source_id == 1);
    ASSERT(entries[1].detail == 5000);

    PASS();
}

void test_error_log_wraparound() {
    TEST(error_log_wraparound);

    auto& log = chopper::safety::ErrorLog::getInstance();
    log.clear();

    // Fill the entire ring buffer
    for (uint32_t i = 0; i < chopper::safety::ErrorLog::LOG_SIZE; ++i) {
        log.log(chopper::safety::ErrorLog::GENERIC_WARNING,
                static_cast<uint8_t>(i & 0xFF), i,
                chopper::safety::ErrorLog::WARNING);
    }

    ASSERT(log.getTotalCount() == chopper::safety::ErrorLog::LOG_SIZE);

    // Write one more — should overwrite the oldest
    log.log(chopper::safety::ErrorLog::GENERIC_ERROR, 0xFF, 9999,
            chopper::safety::ErrorLog::ERROR);
    ASSERT(log.getTotalCount() == chopper::safety::ErrorLog::LOG_SIZE + 1);

    // Read back the most recent entry
    chopper::safety::ErrorLog::Entry entry;
    size_t count = log.readRecent(&entry, 1);
    ASSERT(count == 1);
    ASSERT(entry.category == chopper::safety::ErrorLog::GENERIC_ERROR);
    ASSERT(entry.source_id == 0xFF);
    ASSERT(entry.detail == 9999);

    // Read all entries — should get LOG_SIZE entries max
    chopper::safety::ErrorLog::Entry all[chopper::safety::ErrorLog::LOG_SIZE];
    count = log.readRecent(all, chopper::safety::ErrorLog::LOG_SIZE);
    ASSERT(count == chopper::safety::ErrorLog::LOG_SIZE);

    PASS();
}

void test_error_log_severity_counts() {
    TEST(error_log_severity_counts);

    auto& log = chopper::safety::ErrorLog::getInstance();
    log.clear();

    log.log(chopper::safety::ErrorLog::GENERIC_WARNING, 0, 0,
            chopper::safety::ErrorLog::DEBUG);
    log.log(chopper::safety::ErrorLog::GENERIC_WARNING, 0, 0,
            chopper::safety::ErrorLog::INFO);
    log.log(chopper::safety::ErrorLog::GENERIC_WARNING, 0, 0,
            chopper::safety::ErrorLog::INFO);
    log.log(chopper::safety::ErrorLog::GENERIC_WARNING, 0, 0,
            chopper::safety::ErrorLog::WARNING);
    log.log(chopper::safety::ErrorLog::GENERIC_WARNING, 0, 0,
            chopper::safety::ErrorLog::WARNING);
    log.log(chopper::safety::ErrorLog::GENERIC_WARNING, 0, 0,
            chopper::safety::ErrorLog::WARNING);
    log.log(chopper::safety::ErrorLog::GENERIC_ERROR, 0, 0,
            chopper::safety::ErrorLog::ERROR);
    log.log(chopper::safety::ErrorLog::SAFETY_ESTOP, 0, 0,
            chopper::safety::ErrorLog::FATAL);

    ASSERT(log.getCountBySeverity(chopper::safety::ErrorLog::DEBUG) == 1);
    ASSERT(log.getCountBySeverity(chopper::safety::ErrorLog::INFO) == 2);
    ASSERT(log.getCountBySeverity(chopper::safety::ErrorLog::WARNING) == 3);
    ASSERT(log.getCountBySeverity(chopper::safety::ErrorLog::ERROR) == 1);
    ASSERT(log.getCountBySeverity(chopper::safety::ErrorLog::FATAL) == 1);

    PASS();
}

void test_error_log_concurrent_writes() {
    TEST(error_log_concurrent_writes);

    auto& log = chopper::safety::ErrorLog::getInstance();
    log.clear();

    constexpr int THREADS = 4;
    constexpr int WRITES_PER_THREAD = 100;

    std::vector<std::thread> threads;
    for (int t = 0; t < THREADS; ++t) {
        threads.emplace_back([&log, t]() {
            for (int i = 0; i < WRITES_PER_THREAD; ++i) {
                log.log(chopper::safety::ErrorLog::GENERIC_WARNING,
                        static_cast<uint8_t>(t), static_cast<uint32_t>(i),
                        chopper::safety::ErrorLog::WARNING);
            }
        });
    }

    for (auto& th : threads) th.join();

    ASSERT(log.getTotalCount() == THREADS * WRITES_PER_THREAD);

    PASS();
}

// ---- MotorSafetyMonitor Tests ----

static uint8_t stopped_motor_id = 0xFF;
static int stop_callback_count = 0;

static void test_stop_callback(uint8_t motor_id, void* context) {
    stopped_motor_id = motor_id;
    stop_callback_count++;
}

void test_motor_safety_register_and_feed() {
    TEST(motor_safety_register_and_feed);

    // Clear error log for clean test
    chopper::safety::ErrorLog::getInstance().clear();

    chopper::safety::MotorSafetyMonitor monitor;
    stopped_motor_id = 0xFF;
    stop_callback_count = 0;

    uint8_t id = monitor.registerMotor("left_drive", 100, test_stop_callback, nullptr);
    ASSERT(id != 0xFF);
    ASSERT(monitor.getMotorCount() == 1);

    const auto* motor = monitor.getMotor(id);
    ASSERT(motor != nullptr);
    ASSERT(motor->active);
    ASSERT(motor->enabled);
    ASSERT(!motor->timed_out);
    ASSERT(strcmp(motor->name, "left_drive") == 0);

    // Feed should update last_feed_us
    monitor.feed(id);
    ASSERT(motor->feed_count == 1);

    // Check before timeout — should not fire
    uint64_t now = static_cast<uint64_t>(esp_timer_get_time());
    monitor.checkAll(now);
    ASSERT(stop_callback_count == 0);
    ASSERT(!motor->timed_out);

    PASS();
}

void test_motor_safety_timeout() {
    TEST(motor_safety_timeout);

    chopper::safety::ErrorLog::getInstance().clear();

    chopper::safety::MotorSafetyMonitor monitor;
    stopped_motor_id = 0xFF;
    stop_callback_count = 0;

    // Register with 1ms timeout
    uint8_t id = monitor.registerMotor("test_motor", 1, test_stop_callback, nullptr);
    ASSERT(id != 0xFF);

    // Feed now
    monitor.feed(id);

    // Simulate time passing by checking with a future timestamp
    // 1ms = 1000us timeout, check at 2000us in the future
    uint64_t feed_time = monitor.getMotor(id)->last_feed_us.load(std::memory_order_relaxed);
    uint64_t check_time = feed_time + 2000; // 2ms after feed

    monitor.checkAll(check_time);
    ASSERT(stop_callback_count == 1);
    ASSERT(stopped_motor_id == id);
    ASSERT(monitor.getMotor(id)->timed_out);
    ASSERT(monitor.getMotor(id)->timeout_count == 1);

    // Second check should NOT re-fire (already timed out)
    monitor.checkAll(check_time + 1000);
    ASSERT(stop_callback_count == 1);

    PASS();
}

void test_motor_safety_reset() {
    TEST(motor_safety_reset);

    chopper::safety::ErrorLog::getInstance().clear();

    chopper::safety::MotorSafetyMonitor monitor;
    stopped_motor_id = 0xFF;
    stop_callback_count = 0;

    uint8_t id = monitor.registerMotor("test_motor", 1, test_stop_callback, nullptr);
    monitor.feed(id);

    // Force timeout
    uint64_t feed_time = monitor.getMotor(id)->last_feed_us.load(std::memory_order_relaxed);
    monitor.checkAll(feed_time + 2000);
    ASSERT(monitor.getMotor(id)->timed_out);

    // Reset the motor
    bool reset = monitor.resetMotor(id);
    ASSERT(reset);
    ASSERT(!monitor.getMotor(id)->timed_out);

    // Feed again and check — should not be timed out
    monitor.feed(id);
    uint64_t now = monitor.getMotor(id)->last_feed_us.load(std::memory_order_relaxed);
    monitor.checkAll(now);
    ASSERT(stop_callback_count == 1); // Still only the first timeout

    PASS();
}

void test_motor_safety_disable_all() {
    TEST(motor_safety_disable_all);

    chopper::safety::ErrorLog::getInstance().clear();

    chopper::safety::MotorSafetyMonitor monitor;
    stop_callback_count = 0;

    monitor.registerMotor("motor_0", 100, test_stop_callback, nullptr);
    monitor.registerMotor("motor_1", 100, test_stop_callback, nullptr);
    monitor.registerMotor("motor_2", 100, test_stop_callback, nullptr);

    ASSERT(monitor.getMotorCount() == 3);

    monitor.disableAll();
    ASSERT(stop_callback_count == 3);

    // All motors should be disabled
    for (uint8_t i = 0; i < 3; ++i) {
        ASSERT(!monitor.getMotor(i)->enabled);
    }

    PASS();
}

void test_motor_safety_unregister() {
    TEST(motor_safety_unregister);

    chopper::safety::MotorSafetyMonitor monitor;
    uint8_t id = monitor.registerMotor("temp_motor", 100, test_stop_callback, nullptr);
    ASSERT(monitor.getMotorCount() == 1);
    ASSERT(monitor.getMotor(id) != nullptr);

    monitor.unregisterMotor(id);
    ASSERT(monitor.getMotorCount() == 0);
    ASSERT(monitor.getMotor(id) == nullptr);

    PASS();
}

// ---- DegradationManager Tests ----

static int transition_callback_count = 0;
static chopper::safety::DegradationMode last_old_mode;
static chopper::safety::DegradationMode last_new_mode;

static void test_transition_callback(chopper::safety::DegradationMode old_mode,
                                      chopper::safety::DegradationMode new_mode,
                                      void* context) {
    transition_callback_count++;
    last_old_mode = old_mode;
    last_new_mode = new_mode;
}

void test_degradation_initial_state() {
    TEST(degradation_initial_state);

    chopper::safety::ErrorLog::getInstance().clear();

    chopper::safety::DegradationManager mgr;
    ASSERT(mgr.getCurrentMode() == chopper::safety::DegradationMode::SAFE_STOP);
    ASSERT(mgr.getTransitionCount() == 0);

    PASS();
}

void test_degradation_transition_down() {
    TEST(degradation_transition_down);

    chopper::safety::ErrorLog::getInstance().clear();

    chopper::safety::DegradationManager mgr;
    transition_callback_count = 0;
    mgr.registerTransitionCallback(test_transition_callback, nullptr);

    // Start at SAFE_STOP (initial). Force to FULL first.
    mgr.forceMode(chopper::safety::DegradationMode::FULL_OPERATION);
    ASSERT(mgr.getCurrentMode() == chopper::safety::DegradationMode::FULL_OPERATION);

    // Transition FULL -> REDUCED
    bool ok = mgr.requestTransition(chopper::safety::DegradationMode::REDUCED_FEATURES);
    ASSERT(ok);
    ASSERT(mgr.getCurrentMode() == chopper::safety::DegradationMode::REDUCED_FEATURES);

    // Transition REDUCED -> ESSENTIAL
    ok = mgr.requestTransition(chopper::safety::DegradationMode::ESSENTIAL_ONLY);
    ASSERT(ok);
    ASSERT(mgr.getCurrentMode() == chopper::safety::DegradationMode::ESSENTIAL_ONLY);

    // Transition ESSENTIAL -> SAFE_STOP
    ok = mgr.requestTransition(chopper::safety::DegradationMode::SAFE_STOP);
    ASSERT(ok);
    ASSERT(mgr.getCurrentMode() == chopper::safety::DegradationMode::SAFE_STOP);

    // Transition SAFE_STOP -> EMERGENCY
    ok = mgr.requestTransition(chopper::safety::DegradationMode::EMERGENCY_STOP);
    ASSERT(ok);
    ASSERT(mgr.getCurrentMode() == chopper::safety::DegradationMode::EMERGENCY_STOP);

    PASS();
}

void test_degradation_no_upgrade_via_transition() {
    TEST(degradation_no_upgrade_via_transition);

    chopper::safety::ErrorLog::getInstance().clear();

    chopper::safety::DegradationManager mgr;
    mgr.forceMode(chopper::safety::DegradationMode::ESSENTIAL_ONLY);

    // Attempting to "transition" to a less-degraded mode should fail
    bool ok = mgr.requestTransition(chopper::safety::DegradationMode::FULL_OPERATION);
    ASSERT(!ok);
    ASSERT(mgr.getCurrentMode() == chopper::safety::DegradationMode::ESSENTIAL_ONLY);

    // Same mode should also fail
    ok = mgr.requestTransition(chopper::safety::DegradationMode::ESSENTIAL_ONLY);
    ASSERT(!ok);

    PASS();
}

void test_degradation_upgrade_one_step() {
    TEST(degradation_upgrade_one_step);

    chopper::safety::ErrorLog::getInstance().clear();

    chopper::safety::DegradationManager mgr;
    mgr.forceMode(chopper::safety::DegradationMode::ESSENTIAL_ONLY);

    // Upgrade one step: ESSENTIAL -> REDUCED
    bool ok = mgr.requestUpgrade(chopper::safety::DegradationMode::REDUCED_FEATURES);
    ASSERT(ok);
    ASSERT(mgr.getCurrentMode() == chopper::safety::DegradationMode::REDUCED_FEATURES);

    // Upgrade one more step: REDUCED -> FULL
    ok = mgr.requestUpgrade(chopper::safety::DegradationMode::FULL_OPERATION);
    ASSERT(ok);
    ASSERT(mgr.getCurrentMode() == chopper::safety::DegradationMode::FULL_OPERATION);

    PASS();
}

void test_degradation_no_upgrade_two_steps() {
    TEST(degradation_no_upgrade_two_steps);

    chopper::safety::ErrorLog::getInstance().clear();

    chopper::safety::DegradationManager mgr;
    mgr.forceMode(chopper::safety::DegradationMode::SAFE_STOP);

    // Try to skip two steps: SAFE_STOP -> REDUCED (skip ESSENTIAL)
    bool ok = mgr.requestUpgrade(chopper::safety::DegradationMode::REDUCED_FEATURES);
    ASSERT(!ok);
    ASSERT(mgr.getCurrentMode() == chopper::safety::DegradationMode::SAFE_STOP);

    PASS();
}

void test_degradation_no_upgrade_from_estop() {
    TEST(degradation_no_upgrade_from_estop);

    chopper::safety::ErrorLog::getInstance().clear();

    chopper::safety::DegradationManager mgr;
    mgr.forceMode(chopper::safety::DegradationMode::EMERGENCY_STOP);

    // Upgrade from EMERGENCY_STOP should fail
    bool ok = mgr.requestUpgrade(chopper::safety::DegradationMode::SAFE_STOP);
    ASSERT(!ok);
    ASSERT(mgr.getCurrentMode() == chopper::safety::DegradationMode::EMERGENCY_STOP);

    PASS();
}

void test_degradation_callback_fires() {
    TEST(degradation_callback_fires);

    chopper::safety::ErrorLog::getInstance().clear();

    chopper::safety::DegradationManager mgr;
    transition_callback_count = 0;
    mgr.registerTransitionCallback(test_transition_callback, nullptr);

    mgr.forceMode(chopper::safety::DegradationMode::FULL_OPERATION);
    ASSERT(transition_callback_count == 1);
    ASSERT(last_old_mode == chopper::safety::DegradationMode::SAFE_STOP);
    ASSERT(last_new_mode == chopper::safety::DegradationMode::FULL_OPERATION);

    mgr.requestTransition(chopper::safety::DegradationMode::REDUCED_FEATURES);
    ASSERT(transition_callback_count == 2);
    ASSERT(last_old_mode == chopper::safety::DegradationMode::FULL_OPERATION);
    ASSERT(last_new_mode == chopper::safety::DegradationMode::REDUCED_FEATURES);

    PASS();
}

void test_degradation_mode_to_string() {
    TEST(degradation_mode_to_string);

    ASSERT(strcmp(chopper::safety::degradationModeToString(
        chopper::safety::DegradationMode::FULL_OPERATION), "FULL") == 0);
    ASSERT(strcmp(chopper::safety::degradationModeToString(
        chopper::safety::DegradationMode::REDUCED_FEATURES), "REDUCED") == 0);
    ASSERT(strcmp(chopper::safety::degradationModeToString(
        chopper::safety::DegradationMode::ESSENTIAL_ONLY), "ESSENTIAL") == 0);
    ASSERT(strcmp(chopper::safety::degradationModeToString(
        chopper::safety::DegradationMode::SAFE_STOP), "SAFE_STOP") == 0);
    ASSERT(strcmp(chopper::safety::degradationModeToString(
        chopper::safety::DegradationMode::EMERGENCY_STOP), "ESTOP") == 0);

    PASS();
}

// ---- EmergencyStopChain Tests ----

static int estop_step_order[6] = {0};
static int estop_step_counter = 0;
static const char* estop_last_reason = nullptr;
static uint8_t estop_last_source = 0;

static void estop_step_callback(const char* reason, uint8_t source_id, void* context) {
    int step_index = *static_cast<int*>(context);
    estop_step_order[estop_step_counter++] = step_index;
    estop_last_reason = reason;
    estop_last_source = source_id;
}

void test_estop_chain_execution_order() {
    TEST(estop_chain_execution_order);

    chopper::safety::EmergencyStopChain chain;
    estop_step_counter = 0;

    static int step_ids[6] = {0, 1, 2, 3, 4, 5};

    chain.registerStep(0, "disable_motors", estop_step_callback, &step_ids[0]);
    chain.registerStep(1, "shutdown_drivers", estop_step_callback, &step_ids[1]);
    chain.registerStep(2, "notify_executor", estop_step_callback, &step_ids[2]);
    chain.registerStep(3, "log_event", estop_step_callback, &step_ids[3]);
    chain.registerStep(4, "set_led", estop_step_callback, &step_ids[4]);
    chain.registerStep(5, "rumble_controller", estop_step_callback, &step_ids[5]);

    ASSERT(!chain.isTriggered());

    chain.execute("motor_timeout", 3);

    ASSERT(chain.isTriggered());
    ASSERT(estop_step_counter == 6);

    // Verify order: 0, 1, 2, 3, 4, 5
    for (int i = 0; i < 6; ++i) {
        ASSERT(estop_step_order[i] == i);
    }

    ASSERT(strcmp(estop_last_reason, "motor_timeout") == 0);
    ASSERT(estop_last_source == 3);

    PASS();
}

void test_estop_chain_idempotent() {
    TEST(estop_chain_idempotent);

    chopper::safety::EmergencyStopChain chain;
    estop_step_counter = 0;

    static int step_id = 0;
    chain.registerStep(0, "step0", estop_step_callback, &step_id);

    chain.execute("first_trigger", 1);
    ASSERT(estop_step_counter == 1);

    // Second trigger should be a no-op
    chain.execute("second_trigger", 2);
    ASSERT(estop_step_counter == 1);

    PASS();
}

void test_estop_chain_reset_and_retrigger() {
    TEST(estop_chain_reset_and_retrigger);

    chopper::safety::EmergencyStopChain chain;
    estop_step_counter = 0;

    static int step_id = 0;
    chain.registerStep(0, "step0", estop_step_callback, &step_id);

    chain.execute("trigger1", 1);
    ASSERT(estop_step_counter == 1);
    ASSERT(chain.isTriggered());

    chain.reset();
    ASSERT(!chain.isTriggered());

    chain.execute("trigger2", 2);
    ASSERT(estop_step_counter == 2);

    PASS();
}

void test_estop_chain_skips_null_steps() {
    TEST(estop_chain_skips_null_steps);

    chopper::safety::EmergencyStopChain chain;
    estop_step_counter = 0;

    static int step_ids[6] = {0, 1, 2, 3, 4, 5};

    // Only register steps 0 and 3
    chain.registerStep(0, "step0", estop_step_callback, &step_ids[0]);
    chain.registerStep(3, "step3", estop_step_callback, &step_ids[3]);

    chain.execute("partial", 0);
    ASSERT(estop_step_counter == 2);
    ASSERT(estop_step_order[0] == 0);
    ASSERT(estop_step_order[1] == 3);

    PASS();
}

void test_estop_chain_step_names() {
    TEST(estop_chain_step_names);

    chopper::safety::EmergencyStopChain chain;
    static int dummy = 0;
    chain.registerStep(0, "disable_motors", estop_step_callback, &dummy);
    chain.registerStep(2, "notify_executor", estop_step_callback, &dummy);

    ASSERT(strcmp(chain.getStepName(0), "disable_motors") == 0);
    ASSERT(chain.getStepName(1) == nullptr);
    ASSERT(strcmp(chain.getStepName(2), "notify_executor") == 0);
    ASSERT(chain.getStepName(6) == nullptr); // out of range

    PASS();
}

// ---- SafetyManager Tests ----

void test_safety_manager_emergency_stop() {
    TEST(safety_manager_emergency_stop);

    chopper::safety::ErrorLog::getInstance().clear();

    chopper::safety::SafetyManager mgr;
    ASSERT(!mgr.isEmergencyStopped());

    mgr.emergencyStop("test_reason", 42);
    ASSERT(mgr.isEmergencyStopped());
    ASSERT(mgr.getDegradationManager().getCurrentMode() ==
           chopper::safety::DegradationMode::EMERGENCY_STOP);
    ASSERT(mgr.getEmergencyStopChain().isTriggered());

    // Verify error log has the FATAL entry
    ASSERT(mgr.getErrorLog().getCountBySeverity(chopper::safety::ErrorLog::FATAL) >= 1);

    PASS();
}

void test_safety_manager_reset() {
    TEST(safety_manager_reset);

    chopper::safety::ErrorLog::getInstance().clear();

    chopper::safety::SafetyManager mgr;
    mgr.emergencyStop("test_reset", 0);
    ASSERT(mgr.isEmergencyStopped());

    bool ok = mgr.resetEmergencyStop();
    ASSERT(ok);
    ASSERT(!mgr.isEmergencyStopped());
    ASSERT(!mgr.getEmergencyStopChain().isTriggered());
    ASSERT(mgr.getDegradationManager().getCurrentMode() ==
           chopper::safety::DegradationMode::SAFE_STOP);

    PASS();
}

void test_safety_manager_double_estop() {
    TEST(safety_manager_double_estop);

    chopper::safety::ErrorLog::getInstance().clear();

    chopper::safety::SafetyManager mgr;
    mgr.emergencyStop("first", 1);
    ASSERT(mgr.isEmergencyStopped());

    // Second e-stop should be a no-op
    uint32_t count_before = mgr.getErrorLog().getTotalCount();
    mgr.emergencyStop("second", 2);
    // No additional FATAL log entry
    ASSERT(mgr.getErrorLog().getTotalCount() == count_before);

    PASS();
}

void test_safety_manager_executor_callback() {
    TEST(safety_manager_executor_callback);

    chopper::safety::ErrorLog::getInstance().clear();

    chopper::safety::SafetyManager mgr;

    // Simulate executor calling the static callback
    chopper::safety::SafetyManager::onExecutorEStop("executor_test", &mgr);
    ASSERT(mgr.isEmergencyStopped());

    PASS();
}

void test_safety_manager_update_motor_timeout_degrades() {
    TEST(safety_manager_update_motor_timeout_degrades);

    chopper::safety::ErrorLog::getInstance().clear();

    chopper::safety::SafetyManager mgr;

    // Force to FULL_OPERATION
    mgr.getDegradationManager().forceMode(chopper::safety::DegradationMode::FULL_OPERATION);

    // Register a motor with 1ms timeout
    uint8_t id = mgr.getMotorSafetyMonitor().registerMotor(
        "test_motor", 1, test_stop_callback, nullptr);
    mgr.getMotorSafetyMonitor().feed(id);

    // Update with a timestamp far in the future (motor timed out)
    uint64_t feed_time = mgr.getMotorSafetyMonitor().getMotor(id)->last_feed_us.load(
        std::memory_order_relaxed);
    mgr.update(feed_time + 5000); // 5ms after feed, way past 1ms timeout

    // Should have degraded from FULL to ESSENTIAL_ONLY
    ASSERT(mgr.getDegradationManager().getCurrentMode() ==
           chopper::safety::DegradationMode::ESSENTIAL_ONLY);

    PASS();
}

void test_safety_manager_reset_not_in_estop() {
    TEST(safety_manager_reset_not_in_estop);

    chopper::safety::SafetyManager mgr;
    // Not in e-stop; reset should fail
    bool ok = mgr.resetEmergencyStop();
    ASSERT(!ok);

    PASS();
}

// ---- Main ----

int main() {
    printf("=== Chopper Safety System Tests ===\n\n");

    // ErrorLog
    test_error_log_basic();
    test_error_log_wraparound();
    test_error_log_severity_counts();
    test_error_log_concurrent_writes();

    // MotorSafetyMonitor
    test_motor_safety_register_and_feed();
    test_motor_safety_timeout();
    test_motor_safety_reset();
    test_motor_safety_disable_all();
    test_motor_safety_unregister();

    // DegradationManager
    test_degradation_initial_state();
    test_degradation_transition_down();
    test_degradation_no_upgrade_via_transition();
    test_degradation_upgrade_one_step();
    test_degradation_no_upgrade_two_steps();
    test_degradation_no_upgrade_from_estop();
    test_degradation_callback_fires();
    test_degradation_mode_to_string();

    // EmergencyStopChain
    test_estop_chain_execution_order();
    test_estop_chain_idempotent();
    test_estop_chain_reset_and_retrigger();
    test_estop_chain_skips_null_steps();
    test_estop_chain_step_names();

    // SafetyManager
    test_safety_manager_emergency_stop();
    test_safety_manager_reset();
    test_safety_manager_double_estop();
    test_safety_manager_executor_callback();
    test_safety_manager_update_motor_timeout_degrades();
    test_safety_manager_reset_not_in_estop();

    printf("\n=== Results: %d/%d passed ===\n", pass_count, test_count);
    return (pass_count == test_count) ? 0 : 1;
}
