#pragma once

#include <stdbool.h>
#include "driver/gpio.h"
#include "esp_err.h"

/* 单个雨滴模块的 DO（数字输出）驱动，由同一个任务初始化和读取。
 * active_level：有雨时 DO 的电平，只能填 0 或 1。
 * GPIO 应接模块的 DO，不是 AO；模块与 ESP32 必须共地。
 * 此接口读取即时状态，连续采样确认由应用层完成。 */
typedef struct {
    gpio_num_t gpio;
    int active_level;
    bool initialized;
} rain_sensor_t;

/* 首次使用必须初始化为 {0}。配置一个 DO 输入，并按有效电平选择内部偏置：
 * 低电平有效时启用内部上拉，高电平有效时启用内部下拉。
 * 重复初始化返回 ESP_ERR_INVALID_STATE；配置失败可重试。 */
esp_err_t rain_sensor_init(rain_sensor_t *sensor, gpio_num_t gpio_num, int active_level);

/* 成功时写入 true（有雨）或 false（无雨）。
 * 未初始化返回 ESP_ERR_INVALID_STATE，空指针返回 ESP_ERR_INVALID_ARG。
 * DO 只能判断是否达到模块阈值，不能测量降雨量。 */
esp_err_t rain_sensor_read(rain_sensor_t *sensor, bool *is_raining);
