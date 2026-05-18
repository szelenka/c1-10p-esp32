#pragma once

#include <cstddef>

namespace chopper::limits {

// Core framework limits
constexpr size_t MAX_NODES = 32;
constexpr size_t MAX_TOPICS = 32;
constexpr size_t MAX_SUBSCRIBERS_PER_TOPIC = 8;

// Hardware limits
constexpr size_t MAX_MOTORS = 16;
constexpr size_t MAX_SERVO_CHANNELS = 24;
constexpr size_t MAX_CONTROLLERS = 4;
constexpr size_t MAX_CONTROLLER_MAC_MAPPINGS = 8;
constexpr size_t MAX_DRIVERS = 12;

// Message system limits
constexpr size_t MAX_TIMERS = 16;
constexpr size_t MAX_PARAMETERS = 128;
constexpr size_t MAX_SERVICES = 8;

// Safety system limits
constexpr size_t MAX_ERROR_LOG_ENTRIES = 128;

// FreeRTOS limits
constexpr size_t MAX_TASKS = 8;

// String limits
constexpr size_t MAX_NODE_NAME_LEN = 32;
constexpr size_t MAX_TOPIC_NAME_LEN = 48;
constexpr size_t MAX_PARAM_NAME_LEN = 32;
constexpr size_t MAX_ERROR_MSG_LEN = 64;

}  // namespace chopper::limits
