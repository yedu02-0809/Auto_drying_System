#pragma once

#include <stdint.h>

typedef uint32_t TickType_t;
#ifndef CONFIG_FREERTOS_HZ
#define CONFIG_FREERTOS_HZ 100
#endif
#define pdMS_TO_TICKS(ms) ((TickType_t)(((uint32_t)(ms) * CONFIG_FREERTOS_HZ) / 1000U))
