// Host-side tests for message system enhancements: TimerManager, Service, ParameterServer.
// Compile:
//   g++ -std=c++20 -I test/mocks -I main/include \
//       test/test_message_enhancements.cpp \
//       main/chopper/core/TimerManager.cpp \
//       main/chopper/core/ParameterServer.cpp \
//       -o test/test_message_enhancements -pthread

#include <cstdio>
#include <cassert>
#include <cstring>

#include "chopper/chopper_limits.h"
#include "chopper/core/Message.h"
#include "chopper/core/TimerManager.h"
#include "chopper/core/Service.h"
#include "chopper/core/ParameterServer.h"

// ---- Test helpers ----

static int test_count = 0;
static int pass_count = 0;

#define TEST(name) \
    do { test_count++; printf("TEST: %s ... ", #name); } while(0)
#define PASS() \
    do { pass_count++; printf("PASS\n"); } while(0)
#define ASSERT(cond) \
    do { if (!(cond)) { printf("FAIL at %s:%d: %s\n", __FILE__, __LINE__, #cond); return; } } while(0)

// ========================================================================
// Timer tests
// ========================================================================

void test_timer_basic_firing() {
    TEST(timer_basic_firing);

    auto& tm = chopper::core::TimerManager::getInstance();

    int fire_count = 0;
    auto cb = [](void* ctx) { (*(int*)ctx)++; };

    uint32_t id = tm.createTimer(1000, cb, &fire_count); // 1ms period
    ASSERT(id != 0);
    ASSERT(tm.activeCount() >= 1);

    // First tick initialises next_fire_us; should not fire yet
    tm.tick(0);
    ASSERT(fire_count == 0);

    // Not yet due at t=500
    tm.tick(500);
    ASSERT(fire_count == 0);

    // Due at t=1000
    tm.tick(1000);
    ASSERT(fire_count == 1);

    // Due again at t=2000
    tm.tick(2000);
    ASSERT(fire_count == 2);

    tm.cancelTimer(id);
    tm.tick(3000);
    ASSERT(fire_count == 2); // no more fires after cancel

    PASS();
}

void test_timer_one_shot() {
    TEST(timer_one_shot);

    auto& tm = chopper::core::TimerManager::getInstance();

    int fire_count = 0;
    auto cb = [](void* ctx) { (*(int*)ctx)++; };

    uint32_t id = tm.createTimer(500, cb, &fire_count, /*one_shot=*/true);
    ASSERT(id != 0);

    tm.tick(0);     // init
    tm.tick(500);   // fires once
    ASSERT(fire_count == 1);

    tm.tick(1000);  // should NOT fire again
    ASSERT(fire_count == 1);

    PASS();
}

void test_timer_reset() {
    TEST(timer_reset);

    auto& tm = chopper::core::TimerManager::getInstance();

    int fire_count = 0;
    auto cb = [](void* ctx) { (*(int*)ctx)++; };

    uint32_t id = tm.createTimer(1000, cb, &fire_count);
    ASSERT(id != 0);

    tm.tick(0);     // init: next_fire = 1000
    tm.tick(500);   // not due
    ASSERT(fire_count == 0);

    // Reset at t=500 => next_fire = 500+1000 = 1500
    tm.resetTimer(id, 500);

    tm.tick(1000);  // not due (1000 < 1500)
    ASSERT(fire_count == 0);

    tm.tick(1500);  // due
    ASSERT(fire_count == 1);

    tm.cancelTimer(id);
    PASS();
}

void test_timer_null_callback_rejected() {
    TEST(timer_null_callback_rejected);

    auto& tm = chopper::core::TimerManager::getInstance();
    uint32_t id = tm.createTimer(1000, nullptr, nullptr);
    ASSERT(id == 0);

    PASS();
}

void test_timer_zero_period_rejected() {
    TEST(timer_zero_period_rejected);

    auto& tm = chopper::core::TimerManager::getInstance();
    auto cb = [](void*) {};
    uint32_t id = tm.createTimer(0, cb, nullptr);
    ASSERT(id == 0);

    PASS();
}

// ========================================================================
// Service tests
// ========================================================================

// Simple request/response types for testing
struct PingRequest {
    int sequence;
};

struct PingResponse {
    int sequence;
    bool ok;
};

struct MathRequest {
    int a;
    int b;
};

struct MathResponse {
    int sum;
};

void test_service_call_response() {
    TEST(service_call_response);

    // Server side
    chopper::core::ServiceServer<PingRequest, PingResponse> server;

    auto handler = [](const PingRequest& req, PingResponse& resp, void*) -> bool {
        resp.sequence = req.sequence;
        resp.ok = true;
        return true;
    };

    ASSERT(server.registerHandler(handler));

    // Client side
    chopper::core::ServiceClient<PingRequest, PingResponse> client;

    PingRequest req{42};
    PingResponse resp{};
    auto status = client.call(req, resp);

    using PingStatus = chopper::core::ServiceClient<PingRequest, PingResponse>::Status;
    ASSERT(status == PingStatus::SUCCESS);
    ASSERT(resp.sequence == 42);
    ASSERT(resp.ok == true);

    PASS();
}

void test_service_no_server() {
    TEST(service_no_server);

    chopper::core::ServiceClient<MathRequest, MathResponse> client;

    MathRequest req{1, 2};
    MathResponse resp{};
    auto status = client.call(req, resp);

    using MathStatus = chopper::core::ServiceClient<MathRequest, MathResponse>::Status;
    ASSERT(status == MathStatus::NO_SERVER);

    PASS();
}

void test_service_handler_with_context() {
    TEST(service_handler_with_context);

    struct Multiplier {
        int factor;
    };
    Multiplier mul{10};

    chopper::core::ServiceServer<MathRequest, MathResponse> server;

    auto handler = [](const MathRequest& req, MathResponse& resp, void* ctx) -> bool {
        auto* m = static_cast<Multiplier*>(ctx);
        resp.sum = (req.a + req.b) * m->factor;
        return true;
    };

    ASSERT(server.registerHandler(handler, &mul));

    chopper::core::ServiceClient<MathRequest, MathResponse> client;

    MathRequest req{3, 4};
    MathResponse resp{};
    auto status = client.call(req, resp);

    using MathStatus2 = chopper::core::ServiceClient<MathRequest, MathResponse>::Status;
    ASSERT(status == MathStatus2::SUCCESS);
    ASSERT(resp.sum == 70); // (3+4)*10

    PASS();
}

void test_service_handler_error() {
    TEST(service_handler_error);

    // Define unique types so we don't collide with earlier test registrations
    struct FailReq { int x; };
    struct FailResp { int y; };

    chopper::core::ServiceServer<FailReq, FailResp> server;

    auto handler = [](const FailReq&, FailResp&, void*) -> bool {
        return false; // simulate failure
    };

    ASSERT(server.registerHandler(handler));

    chopper::core::ServiceClient<FailReq, FailResp> client;
    FailReq req{1};
    FailResp resp{};
    auto status = client.call(req, resp);

    using FailStatus = chopper::core::ServiceClient<FailReq, FailResp>::Status;
    ASSERT(status == FailStatus::HANDLER_ERROR);

    PASS();
}

// ========================================================================
// ParameterServer tests
// ========================================================================

void test_param_int32() {
    TEST(param_int32_declare_get_set);

    auto& ps = chopper::core::ParameterServer::getInstance();

    ASSERT(ps.declare("test.int_val", (int32_t)42, (int32_t)0, (int32_t)100));

    int32_t val = 0;
    ASSERT(ps.get("test.int_val", val));
    ASSERT(val == 42);

    ASSERT(ps.set("test.int_val", (int32_t)99));
    ASSERT(ps.get("test.int_val", val));
    ASSERT(val == 99);

    // Out of range should fail
    ASSERT(!ps.set("test.int_val", (int32_t)200));
    ASSERT(ps.get("test.int_val", val));
    ASSERT(val == 99); // unchanged

    PASS();
}

void test_param_float() {
    TEST(param_float_declare_get_set);

    auto& ps = chopper::core::ParameterServer::getInstance();

    ASSERT(ps.declare("test.float_val", 0.5f, 0.0f, 1.0f));

    float val = 0.0f;
    ASSERT(ps.get("test.float_val", val));
    ASSERT(val == 0.5f);

    ASSERT(ps.set("test.float_val", 0.8f));
    ASSERT(ps.get("test.float_val", val));
    ASSERT(val == 0.8f);

    // Out of range
    ASSERT(!ps.set("test.float_val", 1.5f));
    ASSERT(ps.get("test.float_val", val));
    ASSERT(val == 0.8f);

    PASS();
}

void test_param_bool() {
    TEST(param_bool_declare_get_set);

    auto& ps = chopper::core::ParameterServer::getInstance();

    ASSERT(ps.declare("test.flag", false));

    bool val = true;
    ASSERT(ps.get("test.flag", val));
    ASSERT(val == false);

    ASSERT(ps.set("test.flag", true));
    ASSERT(ps.get("test.flag", val));
    ASSERT(val == true);

    PASS();
}

void test_param_type_mismatch() {
    TEST(param_type_mismatch);

    auto& ps = chopper::core::ParameterServer::getInstance();

    ASSERT(ps.declare("test.typed", (int32_t)10));

    // Try to get as float -- should fail
    float fval = 0.0f;
    ASSERT(!ps.get("test.typed", fval));

    // Try to set as bool -- should fail
    ASSERT(!ps.set("test.typed", true));

    PASS();
}

void test_param_not_found() {
    TEST(param_not_found);

    auto& ps = chopper::core::ParameterServer::getInstance();

    int32_t val = 0;
    ASSERT(!ps.get("nonexistent.param", val));
    ASSERT(!ps.set("nonexistent.param", (int32_t)1));

    PASS();
}

void test_param_duplicate_declaration() {
    TEST(param_duplicate_declaration);

    auto& ps = chopper::core::ParameterServer::getInstance();

    ASSERT(ps.declare("test.dup", (int32_t)1));
    ASSERT(!ps.declare("test.dup", (int32_t)2)); // duplicate should fail

    int32_t val = 0;
    ASSERT(ps.get("test.dup", val));
    ASSERT(val == 1); // original value preserved

    PASS();
}

void test_param_on_change() {
    TEST(param_on_change_callback);

    auto& ps = chopper::core::ParameterServer::getInstance();

    ASSERT(ps.declare("test.watched", 0.0f, -10.0f, 10.0f));

    struct CallbackState {
        int call_count;
        char last_name[64];
    };
    CallbackState state{0, {0}};

    auto cb = [](const char* name, void* ctx) {
        auto* s = static_cast<CallbackState*>(ctx);
        s->call_count++;
        strncpy(s->last_name, name, sizeof(s->last_name) - 1);
    };

    ASSERT(ps.onChange("test.watched", cb, &state));

    ps.set("test.watched", 5.0f);
    ASSERT(state.call_count == 1);
    ASSERT(strcmp(state.last_name, "test.watched") == 0);

    ps.set("test.watched", 7.0f);
    ASSERT(state.call_count == 2);

    PASS();
}

void test_param_foreach() {
    TEST(param_foreach_introspection);

    auto& ps = chopper::core::ParameterServer::getInstance();
    size_t expected = ps.count();

    size_t visited = 0;
    ps.forEach([](const chopper::core::Parameter&, void* ctx) {
        (*(size_t*)ctx)++;
    }, &visited);

    ASSERT(visited == expected);

    PASS();
}

// ========================================================================
// Main
// ========================================================================

int main() {
    printf("=== Chopper Message Enhancements Tests ===\n\n");

    // Timer tests
    test_timer_basic_firing();
    test_timer_one_shot();
    test_timer_reset();
    test_timer_null_callback_rejected();
    test_timer_zero_period_rejected();

    // Service tests
    test_service_call_response();
    test_service_no_server();
    test_service_handler_with_context();
    test_service_handler_error();

    // Parameter tests
    test_param_int32();
    test_param_float();
    test_param_bool();
    test_param_type_mismatch();
    test_param_not_found();
    test_param_duplicate_declaration();
    test_param_on_change();
    test_param_foreach();

    printf("\n=== Results: %d/%d passed ===\n", pass_count, test_count);
    return (pass_count == test_count) ? 0 : 1;
}
