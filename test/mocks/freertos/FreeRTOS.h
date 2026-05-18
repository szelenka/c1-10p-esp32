#pragma once
#include <cstdint>
typedef uint32_t TickType_t;
typedef int32_t BaseType_t;
typedef void* TaskHandle_t;
#define pdPASS 1
#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))
#define configMAX_PRIORITIES 25
typedef enum { eDeleted = 4 } eTaskState;
