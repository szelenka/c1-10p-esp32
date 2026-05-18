// Host-side tests for TelemetryService.

#include <cstdio>
#include <cstring>

#include "chopper/telemetry/TelemetryService.h"

static int test_count = 0;
static int pass_count = 0;

#define TEST(name) do { test_count++; std::printf("TEST: %s ... ", #name); } while (0)
#define PASS() do { pass_count++; std::printf("PASS\n"); } while (0)
#define ASSERT(cond) do { if (!(cond)) { std::printf("FAIL at %s:%d: %s\n", __FILE__, __LINE__, #cond); return; } } while (0)

namespace {
char g_last_line[768] = {};

void captureSink(const char* line, void*) {
    std::strncpy(g_last_line, line, sizeof(g_last_line) - 1);
    g_last_line[sizeof(g_last_line) - 1] = '\0';
}

void test_json_format_and_serial_prefix() {
    TEST(json_format_and_serial_prefix);

    chopper::telemetry::TelemetryService svc;
    chopper::telemetry::TelemetryService::Config cfg{};
    cfg.serial_enabled = true;
    cfg.serial_compact = false;
    cfg.include_perf = true;
    cfg.http_enabled = false;
    cfg.websocket_enabled = false;
    cfg.min_publish_interval_ms = 0;

    svc.setSerialSink(&captureSink, nullptr);
    ASSERT(svc.begin(cfg));

    chopper::telemetry::TelemetryService::Snapshot snap{};
    snap.timestamp_us = 12345;
    snap.loop_count = 99;
    snap.max_loop_time_us = 1500;
    snap.avg_loop_time_us = 700;
    snap.active_nodes = 4;
    snap.total_nodes = 7;
    snap.executor_estop = false;
    snap.safety_estop = true;
    snap.degradation_mode = 3;

    svc.update(snap);

    ASSERT(std::strncmp(g_last_line, "TEL:{", 5) == 0);
    ASSERT(std::strstr(g_last_line, "\"loop_count\":99") != nullptr);
    ASSERT(std::strstr(g_last_line, "\"degradation_mode\":3") != nullptr);
    ASSERT(std::strstr(svc.getLastJson(), "\"safety_estop\":true") != nullptr);

    svc.shutdown();
    PASS();
}

void test_min_publish_interval() {
    TEST(min_publish_interval);

    std::memset(g_last_line, 0, sizeof(g_last_line));

    chopper::telemetry::TelemetryService svc;
    chopper::telemetry::TelemetryService::Config cfg{};
    cfg.serial_enabled = true;
    cfg.serial_compact = false;
    cfg.include_perf = true;
    cfg.http_enabled = false;
    cfg.websocket_enabled = false;
    cfg.min_publish_interval_ms = 100;

    svc.setSerialSink(&captureSink, nullptr);
    ASSERT(svc.begin(cfg));

    chopper::telemetry::TelemetryService::Snapshot a{};
    a.timestamp_us = 1000000;
    a.loop_count = 1;
    svc.update(a);
    ASSERT(std::strstr(g_last_line, "\"loop_count\":1") != nullptr);

    chopper::telemetry::TelemetryService::Snapshot b{};
    b.timestamp_us = 1050000;  // +50ms, should be skipped
    b.loop_count = 2;
    svc.update(b);
    ASSERT(std::strstr(g_last_line, "\"loop_count\":1") != nullptr);

    chopper::telemetry::TelemetryService::Snapshot c{};
    c.timestamp_us = 1110000;  // +110ms, should emit
    c.loop_count = 3;
    svc.update(c);
    ASSERT(std::strstr(g_last_line, "\"loop_count\":3") != nullptr);

    svc.shutdown();
    PASS();
}
}  // namespace

int main() {
    std::printf("\n=== Telemetry Tests ===\n\n");

    test_json_format_and_serial_prefix();
    test_min_publish_interval();

    std::printf("\n=== Results: %d/%d passed ===\n", pass_count, test_count);
    return (pass_count == test_count) ? 0 : 1;
}
