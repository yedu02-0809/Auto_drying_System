#pragma once

#include <stdint.h>
#include "esp_err.h"

typedef int gpio_num_t;
enum { GPIO_NUM_MAX = 40 };
enum { GPIO_MODE_INPUT = 1,
       GPIO_PULLUP_ENABLE = 1, GPIO_PULLUP_DISABLE = 0,
       GPIO_PULLDOWN_ENABLE = 1, GPIO_PULLDOWN_DISABLE = 0,
       GPIO_INTR_DISABLE = 0 };

/* 经典 ESP32 的 GPIO34~39 只能输入，因此没有内部上下拉。 */
#define GPIO_IS_VALID_GPIO(gpio) ((gpio) >= 0 && (gpio) < GPIO_NUM_MAX)

/* ESP32 output-capable pads also support the driver's internal pull-up. */
#define GPIO_IS_VALID_OUTPUT_GPIO(gpio) \
    ((gpio) >= 0 && (gpio) < 34 && (gpio) != 20 && (gpio) != 24 && \
     !((gpio) >= 28 && (gpio) <= 31))

typedef struct {
    uint64_t pin_bit_mask;
    int mode;
    int pull_up_en;
    int pull_down_en;
    int intr_type;
} gpio_config_t;

esp_err_t gpio_config(const gpio_config_t *config);
int gpio_get_level(gpio_num_t gpio);
