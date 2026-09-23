#pragma once
#include "FreeRTOS.h"
typedef struct mock_queue *QueueHandle_t;
QueueHandle_t xQueueCreate(UBaseType_t length, UBaseType_t size);
BaseType_t xQueuePeek(QueueHandle_t queue, void *output, TickType_t ticks);
BaseType_t xQueueSend(QueueHandle_t queue, const void *input, TickType_t ticks);
BaseType_t xQueueReceive(QueueHandle_t queue, void *output, TickType_t ticks);
BaseType_t xQueueOverwrite(QueueHandle_t queue, const void *input);
void vQueueDelete(QueueHandle_t queue);
