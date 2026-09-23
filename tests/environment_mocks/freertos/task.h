#pragma once
#include "FreeRTOS.h"
typedef void (*TaskFunction_t)(void *);
BaseType_t xTaskCreate(TaskFunction_t task, const char *name, uint32_t stack,
                      void *argument, UBaseType_t priority, void *handle);
void vTaskDelay(TickType_t ticks);
