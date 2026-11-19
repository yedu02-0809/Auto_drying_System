#pragma once

#include <stdbool.h>
#include "driver/gpio.h"
#include "esp_err.h"

/* 单舵机驱动，占用 LEDC 低速模式、定时器 0、通道 0。
 * 因为 LEDC 通道是全局资源，整个固件只能存在一个已初始化的 servo_t。
 * 由同一个任务调用；angle 是目标角度，不是实际位置反馈。 */
typedef struct {
    gpio_num_t gpio;
    bool initialized;
} servo_t;

/* 首次使用必须初始化为 {0}。配置 PWM 并输出 90° 中位。
 * 重复初始化返回 ESP_ERR_INVALID_STATE；失败时不产生 PWM 输出，可重试。 */
esp_err_t servo_init(servo_t *servo, gpio_num_t gpio_num);

/* 设置目标角度（0..180，单位度）。只下发 PWM，不等待舵机转到位。
 * 未初始化返回 ESP_ERR_INVALID_STATE；非有限值或越界返回 ESP_ERR_INVALID_ARG。 */
esp_err_t servo_set_angle(servo_t *servo, float angle);
