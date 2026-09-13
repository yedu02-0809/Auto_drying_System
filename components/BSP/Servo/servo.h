#pragma once

#include "driver/gpio.h"
#include "esp_err.h"

/* 单舵机驱动，占用 LEDC 低速通道 0、定时器 0。
 * 由同一个任务调用；angle 是目标角度，不是实际位置反馈。 */
esp_err_t servo_init(gpio_num_t gpio_num);
esp_err_t servo_set_angle(float angle);

