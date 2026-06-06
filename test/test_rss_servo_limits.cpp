#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include <array>
#include <cstdint>
#include <memory>

#include "chopper/config/DefaultParameters.h"
#include "chopper/core/MessageBroker.h"
#include "chopper/core/ParameterServer.h"
#include "chopper/dome/RSSMechanism.h"
#include "chopper/messages/CommonMessages.h"
#include "chopper/nodes/NeckNode.h"

namespace {

struct ServoLimit {
    uint16_t min;
    uint16_t max;
    const char* min_name;
    const char* max_name;
    const char* neutral_name;
};

constexpr std::array<ServoLimit, 3> C034_NECK_LIMITS = {{
    {2032, 2256, "servo.neck_a.min", "servo.neck_a.max", "servo.neck_a.neutral"},
    {1952, 2176, "servo.neck_b.min", "servo.neck_b.max", "servo.neck_b.neutral"},
    {2048, 2272, "servo.neck_c.min", "servo.neck_c.max", "servo.neck_c.neutral"},
}};

chopper::dome::RSSMechanism makeC034Mechanism() {
    chopper::dome::RSSMechanism mech(149.053f, 193.350f, 45.0f, 31.0f, 28.621f, 0.25f, true);
    mech.setActuationRange(270);
    mech.setRotationAngleOffset(-30.0f);
    mech.setLegMinPulse(C034_NECK_LIMITS[0].min, C034_NECK_LIMITS[1].min, C034_NECK_LIMITS[2].min);
    mech.setLegMaxPulse(C034_NECK_LIMITS[0].max, C034_NECK_LIMITS[1].max, C034_NECK_LIMITS[2].max);
    mech.setEnabled(true, 0);
    return mech;
}

void checkWithinC034Limits(const std::array<uint16_t, 3>& pwm) {
    for (size_t i = 0; i < pwm.size(); ++i) {
        CAPTURE(i);
        CAPTURE(pwm[i]);
        CHECK(pwm[i] >= C034_NECK_LIMITS[i].min);
        CHECK(pwm[i] <= C034_NECK_LIMITS[i].max);
    }
}

}  // namespace

TEST_CASE("neck servo parameters cannot exceed c034f87 A/B/C limits") {
    auto& ps = chopper::core::ParameterServer::getInstance();
    ps.reset();
    chopper::config::registerDefaultParameters();

    for (const ServoLimit& limit : C034_NECK_LIMITS) {
        int32_t value = 0;
        CHECK(ps.get(limit.min_name, value));
        CHECK(value == limit.min);
        CHECK(ps.get(limit.max_name, value));
        CHECK(value == limit.max);

        CHECK_FALSE(ps.set(limit.min_name, static_cast<int32_t>(limit.min) - 1));
        CHECK_FALSE(ps.set(limit.max_name, static_cast<int32_t>(limit.max) + 1));
        CHECK_FALSE(ps.set(limit.neutral_name, static_cast<int32_t>(limit.min) - 1));
        CHECK_FALSE(ps.set(limit.neutral_name, static_cast<int32_t>(limit.max) + 1));
    }
}

TEST_CASE("RSS mechanism clamps computed PWM to c034f87 A/B/C limits") {
    auto mech = makeC034Mechanism();

    const float min_height = mech.getMinHeight();
    const float max_height = mech.getMaxHeight();
    const std::array<float, 5> heights = {
        min_height - 50.0f,
        min_height,
        (min_height + max_height) / 2.0f,
        max_height,
        max_height + 50.0f,
    };
    const std::array<float, 7> inputs = {-2.0f, -1.0f, -0.5f, 0.0f, 0.5f, 1.0f, 2.0f};

    for (const float height : heights) {
        mech.setHeight(height);
        for (const float x : inputs) {
            for (const float y : inputs) {
                CAPTURE(height);
                CAPTURE(x);
                CAPTURE(y);
                checkWithinC034Limits(mech.getLegPWMFromJoystick(x, y, 2000));
            }
        }
    }
}

TEST_CASE("NeckNode publishes only c034f87-bounded RSS servo commands") {
    auto mech = makeC034Mechanism();
    auto node = std::make_shared<chopper::nodes::NeckNode>(&mech, "controller/rss_limit_input",
                                                           "servo/rss_limit_output", 0, 1, 2);
    REQUIRE(node->initialize());
    node->activate();
    node->setTime(2000);

    struct Capture {
        std::array<int, 3> count = {0, 0, 0};
        bool out_of_range = false;
    } capture;

    auto& broker = chopper::core::MessageBroker::getInstance();
    auto sub = broker.createSubscription<chopper::messages::ServoCommand>(
        "servo/rss_limit_output",
        [](const chopper::messages::ServoCommand& cmd, void* ctx) {
            auto* out = static_cast<Capture*>(ctx);
            if (cmd.command_type != chopper::messages::ServoCommand::CommandType::SET_POSITION || cmd.servo_id >= 3) {
                return;
            }
            const auto pulse = static_cast<uint16_t>(cmd.value);
            if (pulse < C034_NECK_LIMITS[cmd.servo_id].min || pulse > C034_NECK_LIMITS[cmd.servo_id].max) {
                out->out_of_range = true;
            }
            out->count[cmd.servo_id]++;
        },
        &capture);

    auto pub = broker.createPublisher<chopper::messages::ControllerInput>("controller/rss_limit_input");
    const std::array<float, 5> inputs = {-1.0f, -0.5f, 0.0f, 0.5f, 1.0f};

    for (const float x : inputs) {
        for (const float y : inputs) {
            chopper::messages::ControllerInput input{};
            input.is_connected = true;
            input.has_data = true;
            input.has_intents = true;
            input.axis_x_slew = x;
            input.axis_y_slew = y;
            pub->publish(input);
        }
    }

    CHECK_FALSE(capture.out_of_range);
    CHECK(capture.count[0] > 0);
    CHECK(capture.count[1] > 0);
    CHECK(capture.count[2] > 0);
}

TEST_CASE("NeckNode scales full joystick travel to c034f87 RSS input range") {
    auto expected_mech = makeC034Mechanism();
    const auto expected = expected_mech.getLegPWMFromJoystick(0.25f, 0.0f, 2000);
    const auto unscaled = expected_mech.getLegPWMFromJoystick(1.0f, 0.0f, 2000);

    auto mech = makeC034Mechanism();
    auto node = std::make_shared<chopper::nodes::NeckNode>(&mech, "controller/rss_scaled_input",
                                                           "servo/rss_scaled_output", 0, 1, 2);
    REQUIRE(node->initialize());
    node->activate();
    node->setTime(2000);

    struct Capture {
        std::array<uint16_t, 3> pwm = {0, 0, 0};
        std::array<int, 3> count = {0, 0, 0};
    } capture;

    auto& broker = chopper::core::MessageBroker::getInstance();
    auto sub = broker.createSubscription<chopper::messages::ServoCommand>(
        "servo/rss_scaled_output",
        [](const chopper::messages::ServoCommand& cmd, void* ctx) {
            auto* out = static_cast<Capture*>(ctx);
            if (cmd.command_type != chopper::messages::ServoCommand::CommandType::SET_POSITION || cmd.servo_id >= 3) {
                return;
            }
            out->pwm[cmd.servo_id] = static_cast<uint16_t>(cmd.value);
            out->count[cmd.servo_id]++;
        },
        &capture);

    auto pub = broker.createPublisher<chopper::messages::ControllerInput>("controller/rss_scaled_input");
    chopper::messages::ControllerInput input{};
    input.is_connected = true;
    input.has_data = true;
    input.has_intents = true;
    input.axis_x_slew = 1.0f;
    input.axis_y_slew = 0.0f;
    pub->publish(input);

    CHECK(capture.count[0] == 1);
    CHECK(capture.count[1] == 1);
    CHECK(capture.count[2] == 1);
    CHECK(capture.pwm == expected);
    CHECK(capture.pwm[0] != unscaled[0]);
    CHECK(capture.pwm[0] > C034_NECK_LIMITS[0].min);
    CHECK(capture.pwm[0] < C034_NECK_LIMITS[0].max);
}

TEST_CASE("single neck thumb click does not enable RSS servos") {
    chopper::dome::RSSMechanism mech(149.053f, 193.350f, 45.0f, 31.0f, 28.621f, 0.25f, true);
    mech.setActuationRange(270);
    mech.setLegMinPulse(C034_NECK_LIMITS[0].min, C034_NECK_LIMITS[1].min, C034_NECK_LIMITS[2].min);
    mech.setLegMaxPulse(C034_NECK_LIMITS[0].max, C034_NECK_LIMITS[1].max, C034_NECK_LIMITS[2].max);

    auto node = std::make_shared<chopper::nodes::NeckNode>(&mech, "controller/rss_single_click_input",
                                                           "servo/rss_single_click_output", 0, 1, 2);
    REQUIRE(node->initialize());
    node->activate();
    node->setTime(2000);

    int command_count = 0;
    auto& broker = chopper::core::MessageBroker::getInstance();
    auto sub = broker.createSubscription<chopper::messages::ServoCommand>(
        "servo/rss_single_click_output",
        [](const chopper::messages::ServoCommand&, void* ctx) {
            auto* count = static_cast<int*>(ctx);
            (*count)++;
        },
        &command_count);

    auto pub = broker.createPublisher<chopper::messages::ControllerInput>("controller/rss_single_click_input");
    chopper::messages::ControllerInput input{};
    input.is_connected = true;
    input.has_data = true;
    input.has_intents = true;
    input.intent_neck_toggle = true;
    pub->publish(input);

    CHECK_FALSE(mech.isEnabled());
    CHECK(command_count == 0);
}
