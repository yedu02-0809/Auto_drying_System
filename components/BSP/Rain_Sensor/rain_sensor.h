#pragma once

#include <stdbool.h>
#include "driver/gpio.h"
#include "esp_err.h"

/* 单个雨滴模块的 DO（数字输出）驱动，由同一个任务初始化和读取。
 * active_level：有雨时 DO 的电平，只能填 0 或 1。
 * GPIO 应接模块的 DO，不是 AO；模块与 ESP32 必须共地。
 * 此接口读取即时状态，连续采样确认由应用层完成。 */
esp_err_t rain_sensor_init(gpio_num_t gpio_num, int active_level);

/* 成功时写入 true（有雨）或 false（无雨）。
 * 未初始化返回 ESP_ERR_INVALID_STATE，空指针返回 ESP_ERR_INVALID_ARG。
 * DO 只能判断是否达到模块阈值，不能测量降雨量。 */
esp_err_t rain_sensor_read(bool *is_raining);
