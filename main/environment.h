#pragma once

#include <stdbool.h>
#include "esp_err.h"
#include "rtc_ds3231.h"

/* 启动共享 I2C0 的 SHT30/RTC/BH1750 采集任务，以及串口 rtc_get/rtc_set 命令。 */
esp_err_t environment_start(void);

/* 非阻塞读取最近一次有效的 RTC 时间，超过 3 秒未更新则返回 false。
 * RTC 存储当地日历时间（本项目约定北京时间），不进行时区转换。 */
bool environment_get_time(rtc_datetime_t *time);

/* 非阻塞读取最近一次成功的温湿度读数（摄氏度、%RH）。
 * 采样周期 2 秒；超过 5 秒未更新或最近一次读取失败时返回 false，且不修改输出。 */
bool environment_get_climate(float *temperature_c, float *humidity_percent);

/* 非阻塞读取最近一次成功的光照读数（lux），过期规则同 environment_get_climate()。 */
bool environment_get_lux(float *lux);
