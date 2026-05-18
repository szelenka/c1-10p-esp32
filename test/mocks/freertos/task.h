#pragma once
#include "FreeRTOS.h"
inline BaseType_t xTaskCreate(void(*fn)(void*), const char*, uint32_t, void*, int, TaskHandle_t*) {
    return pdPASS;
}
inline void vTaskDelay(TickType_t) {}
inline void vTaskDelayUntil(TickType_t*, TickType_t) {}
inline void vTaskDelete(TaskHandle_t) {}
inline TickType_t xTaskGetTickCount() { return 0; }
inline eTaskState eTaskGetState(TaskHandle_t) { return eDeleted; }
inline void taskYIELD() {}
