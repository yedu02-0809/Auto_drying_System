#pragma once

#include <stdint.h>
#include "driver/i2c_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* DS3231 保存当地墙上时间；本驱动限定公历 2000..2099 年，使用 24 小时制。 */
typedef struct {
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
} rtc_datetime_t;

/* 首次使用必须初始化为 {0}，同一对象的所有操作由同一任务串行调用。 */
typedef struct {
    i2c_master_dev_handle_t i2c_dev;
} rtc_t;

/* 在调用者的共享 I2C 总线上添加 0x68 设备，启用电池后备振荡器。
 * 不改时间、不清失效标记；若失败且清理也失败，保留句柄供 deinit 重试。 */
esp_err_t rtc_ds3231_init(rtc_t *rtc, i2c_master_bus_handle_t bus);

/* 读取完整时间快照；OSF=1 返回 ESP_ERR_INVALID_STATE，需要明确校时。
 * 非法 BCD/日期返回 ESP_ERR_INVALID_RESPONSE，所有失败均不修改输出。 */
esp_err_t rtc_ds3231_get_datetime(rtc_t *rtc, rtc_datetime_t *datetime);

/* 写入有效当地时间，再清 OSF，保留其他状态位。I2C 错误原样返回；
 * 若写时间已成功而清 OSF 失败，仍返回错误，可重试完整校时。 */
esp_err_t rtc_ds3231_set_datetime(rtc_t *rtc, const rtc_datetime_t *datetime);

/* 仅删除设备，不释放共享总线；删除失败保留句柄供重试。 */
esp_err_t rtc_ds3231_deinit(rtc_t *rtc);

#ifdef __cplusplus
}
#endif
