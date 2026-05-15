// Host-side tests for MP3AudioDriver.
// Verifies the MP3 Trigger serial protocol and driver lifecycle.
//
// Compile:
//   g++ -std=c++20 -I test/mocks -I main/include \
//       test/test_mp3trigger.cpp -o build/test/test_mp3trigger -pthread

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <vector>
#include <queue>

#include "chopper/hal/ISerialPort.h"
#include "chopper/hal/MP3AudioDriver.h"

// Bidirectional mock serial port: records writes AND supports injecting read data
class MockBidirectionalSerialPort : public chopper::hal::ISerialPort {
public:
    std::vector<uint8_t> writtenBytes;
    std::queue<uint8_t> readBuffer;

    size_t write(const uint8_t* data, size_t length) override {
        for (size_t i = 0; i < length; i++) {
            writtenBytes.push_back(data[i]);
        }
        return length;
    }

    int available() override {
        return static_cast<int>(readBuffer.size());
    }

    int read() override {
        if (readBuffer.empty()) return -1;
        uint8_t byte = readBuffer.front();
        readBuffer.pop();
        return byte;
    }

    void clearWritten() { writtenBytes.clear(); }

    // Inject bytes that the driver will "read" from the board
    void injectResponse(uint8_t byte) { readBuffer.push(byte); }
    void injectResponse(const uint8_t* data, size_t len) {
        for (size_t i = 0; i < len; i++) readBuffer.push(data[i]);
    }
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
    TEST(mp3_init_lifecycle);

    MockBidirectionalSerialPort serial;
    chopper::hal::MP3AudioDriver driver(serial, "test_mp3");

    ASSERT(driver.getStatus() == chopper::hal::DriverStatus::kUninitialized);

    auto status = driver.init();
    ASSERT(status == chopper::hal::DriverStatus::kReady);
    ASSERT(driver.getStatus() == chopper::hal::DriverStatus::kReady);

    driver.shutdown();
    ASSERT(driver.getStatus() == chopper::hal::DriverStatus::kDisabled);

    PASS();
}

void test_trigger_sends_correct_bytes() {
    TEST(mp3_trigger_sends_t_track);

    MockBidirectionalSerialPort serial;
    chopper::hal::MP3AudioDriver driver(serial, "test");
    driver.init();

    serial.clearWritten();
    driver.trigger(5);

    // Should send ['t', 5]
    ASSERT(serial.writtenBytes.size() == 2);
    ASSERT(serial.writtenBytes[0] == 't');
    ASSERT(serial.writtenBytes[1] == 5);

    PASS();
}

void test_set_volume_sends_correct_bytes() {
    TEST(mp3_set_volume_sends_v_vol);

    MockBidirectionalSerialPort serial;
    chopper::hal::MP3AudioDriver driver(serial, "test");
    driver.init();

    serial.clearWritten();
    driver.setVolume(200);

    // Should send ['v', 200]
    ASSERT(serial.writtenBytes.size() == 2);
    ASSERT(serial.writtenBytes[0] == 'v');
    ASSERT(serial.writtenBytes[1] == 200);
    ASSERT(driver.getVolume() == 200);

    PASS();
}

void test_stop_sends_correct_byte() {
    TEST(mp3_stop_sends_O);

    MockBidirectionalSerialPort serial;
    chopper::hal::MP3AudioDriver driver(serial, "test");
    driver.init();

    driver.trigger(1);  // Start playing
    serial.clearWritten();

    driver.stop();

    // Should send 'O'
    ASSERT(serial.writtenBytes.size() == 1);
    ASSERT(serial.writtenBytes[0] == 'O');
    ASSERT(!driver.isPlaying());

    PASS();
}

void test_stop_when_idle_sends_no_bytes() {
    TEST(mp3_stop_when_idle_sends_no_bytes);

    MockBidirectionalSerialPort serial;
    chopper::hal::MP3AudioDriver driver(serial, "test");
    driver.init();

    serial.clearWritten();
    driver.stop();

    ASSERT(serial.writtenBytes.empty());

    PASS();
}

void test_trigger_sets_playing() {
    TEST(mp3_trigger_sets_playing_true);

    MockBidirectionalSerialPort serial;
    chopper::hal::MP3AudioDriver driver(serial, "test");
    driver.init();

    ASSERT(!driver.isPlaying());

    driver.trigger(1);
    ASSERT(driver.isPlaying());

    PASS();
}

void test_update_processes_track_end() {
    TEST(mp3_update_processes_X_response);

    MockBidirectionalSerialPort serial;
    chopper::hal::MP3AudioDriver driver(serial, "test");
    driver.init();

    driver.trigger(1);
    ASSERT(driver.isPlaying());

    // Inject 'X' (track finished) response
    serial.injectResponse('X');
    driver.update();

    ASSERT(!driver.isPlaying());

    PASS();
}

void test_update_processes_lowercase_idle_track_end() {
    TEST(mp3_update_processes_x_response_when_idle);

    MockBidirectionalSerialPort serial;
    chopper::hal::MP3AudioDriver driver(serial, "test");
    driver.init();

    ASSERT(!driver.isPlaying());

    serial.injectResponse('x');
    driver.update();

    ASSERT(!driver.isPlaying());

    PASS();
}

void test_update_processes_error() {
    TEST(mp3_update_processes_E_response);

    MockBidirectionalSerialPort serial;
    chopper::hal::MP3AudioDriver driver(serial, "test");
    driver.init();

    driver.trigger(1);
    ASSERT(driver.isPlaying());

    // Inject 'E' (error) response
    serial.injectResponse('E');
    driver.update();

    ASSERT(!driver.isPlaying());

    PASS();
}

void test_update_processes_trigger_input() {
    TEST(mp3_update_processes_M_response);

    MockBidirectionalSerialPort serial;
    chopper::hal::MP3AudioDriver driver(serial, "test");
    driver.init();

    driver.trigger(1);

    // Inject 'M' followed by 3 status bytes (trigger input event)
    serial.injectResponse('M');
    serial.injectResponse(0x01);
    serial.injectResponse(0x02);
    serial.injectResponse(0x03);

    driver.update();

    // 'M' should be consumed along with 3 following bytes, playing state unchanged
    ASSERT(driver.isPlaying());
    ASSERT(serial.available() == 0);

    PASS();
}

void test_random_track_selection() {
    TEST(mp3_random_track_from_pool);

    MockBidirectionalSerialPort serial;
    chopper::hal::MP3AudioDriver driver(serial, "test");

    driver.addRandomTrack(10);
    driver.addRandomTrack(20);
    driver.addRandomTrack(30);
    driver.init();

    serial.clearWritten();
    driver.triggerRandom();

    // Should have triggered some track: 2 bytes written
    ASSERT(serial.writtenBytes.size() == 2);
    ASSERT(serial.writtenBytes[0] == 't');
    // Track should be one of {10, 20, 30}
    uint8_t track = serial.writtenBytes[1];
    ASSERT(track == 10 || track == 20 || track == 30);

    PASS();
}

void test_random_track_empty_pool() {
    TEST(mp3_random_track_empty_pool_noop);

    MockBidirectionalSerialPort serial;
    chopper::hal::MP3AudioDriver driver(serial, "test");
    driver.init();

    serial.clearWritten();
    driver.triggerRandom();

    // Empty pool — nothing written
    ASSERT(serial.writtenBytes.empty());

    PASS();
}

void test_add_random_track_overflow() {
    TEST(mp3_add_random_track_overflow);

    MockBidirectionalSerialPort serial;
    chopper::hal::MP3AudioDriver driver(serial, "test");

    // Fill up the pool
    for (int i = 0; i < chopper::hal::MP3AudioDriver::kMaxRandomTracks; i++) {
        ASSERT(driver.addRandomTrack(static_cast<uint8_t>(i)));
    }

    // 33rd should fail
    ASSERT(!driver.addRandomTrack(99));

    PASS();
}

void test_update_skips_when_not_ready() {
    TEST(mp3_update_skips_when_not_ready);

    MockBidirectionalSerialPort serial;
    chopper::hal::MP3AudioDriver driver(serial, "test");
    // Don't call init()

    serial.injectResponse('X');
    driver.update();

    // Response byte should NOT have been consumed (update skipped)
    ASSERT(serial.available() == 1);

    PASS();
}

void test_trigger_skips_when_not_ready() {
    TEST(mp3_trigger_skips_when_not_ready);

    MockBidirectionalSerialPort serial;
    chopper::hal::MP3AudioDriver driver(serial, "test");
    // Don't call init()

    driver.trigger(5);

    // Nothing written
    ASSERT(serial.writtenBytes.empty());
    ASSERT(!driver.isPlaying());

    PASS();
}

void test_reset_clears_state() {
    TEST(mp3_reset_clears_state);

    MockBidirectionalSerialPort serial;
    chopper::hal::MP3AudioDriver driver(serial, "test");
    driver.init();

    driver.setVolume(100);
    driver.trigger(5);
    ASSERT(driver.isPlaying());

    auto status = driver.reset();
    ASSERT(status == chopper::hal::DriverStatus::kReady);
    ASSERT(driver.getVolume() == 0);
    ASSERT(!driver.isPlaying());

    PASS();
}

void test_diagnostic_status() {
    TEST(mp3_diagnostic_status);

    MockBidirectionalSerialPort serial;
    chopper::hal::MP3AudioDriver driver(serial, "test_diag");
    driver.init();
    driver.setVolume(50);

    char response[128] = {0};
    ASSERT(driver.handleDiagnostic("status", response, sizeof(response)));
    ASSERT(strstr(response, "READY") != nullptr);
    ASSERT(strstr(response, "volume=50") != nullptr);

    PASS();
}

// ---- Main ----

int main() {
    printf("=== MP3 Audio Driver Tests ===\n\n");

    test_init_lifecycle();
    test_trigger_sends_correct_bytes();
    test_set_volume_sends_correct_bytes();
    test_stop_sends_correct_byte();
    test_stop_when_idle_sends_no_bytes();
    test_trigger_sets_playing();
    test_update_processes_track_end();
    test_update_processes_lowercase_idle_track_end();
    test_update_processes_error();
    test_update_processes_trigger_input();
    test_random_track_selection();
    test_random_track_empty_pool();
    test_add_random_track_overflow();
    test_update_skips_when_not_ready();
    test_trigger_skips_when_not_ready();
    test_reset_clears_state();
    test_diagnostic_status();

    printf("\n=== Results: %d/%d passed ===\n", pass_count, test_count);
    return (pass_count == test_count) ? 0 : 1;
}
