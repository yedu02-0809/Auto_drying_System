#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "driver/i2c_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BH1750_I2C_ADDRESS_DEFAULT   0x23
#define BH1750_I2C_ADDRESS_ALTERNATE 0x5C

/* 首次使用初始化为 {0}；同一对象的初始化、读取和释放须串行调用。
 * I2C 总线由调用者创建、管理，可与 SHT30 和 DS3231 共享。 */
typedef struct {
    i2c_master_dev_handle_t i2c_dev;
    bool initialized;
} bh1750_t;

/* 添加 7 位地址设备（不要左移），明确设置 MTreg=69 后进入低功耗待机。
 * 失败会尝试删除设备；若删除也失败，保留句柄供 deinit 重试清理。 */
esp_err_t bh1750_init(bh1750_t *sensor, i2c_master_bus_handle_t bus, uint8_t address);

/* 单次高分辨率模式读取照度（lux），等待至少 180ms，须在任务中调用。
 * 测量后芯片自动进入 power-down；通信失败不修改 lux。 */
esp_err_t bh1750_read_lux(bh1750_t *sensor, float *lux);

/* 仅删除设备，不删除共享总线；失败保留对象供重试。 */
esp_err_t bh1750_deinit(bh1750_t *sensor);

#ifdef __cplusplus
}
#endif
