#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "driver/i2c_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SHT30_I2C_ADDRESS_DEFAULT 0x44
#define SHT30_I2C_ADDRESS_ALTERNATE 0x45

/* 沿用项目预建的 SHT20 目录，但本驱动仅实现 SHT30/SHT3x-DIS 协议。
 * 每个对象由同一任务操作，首次使用必须初始化为 {0}。
 * I2C 总线由调用者创建和管理，可与其他 I2C 设备共享。 */
typedef struct {
    i2c_master_dev_handle_t i2c_dev;
    bool initialized;
} sht30_t;

/* 添加设备并软复位；address 是 7 位地址 0x44 或 0x45，不要左移。
 * 失败时会尝试释放设备。若释放也失败，保留 i2c_dev，调用者应使用
 * sht30_deinit() 重试清理，再重新初始化。不会释放调用者的 I2C 总线。 */
esp_err_t sht30_init(sht30_t *sensor, i2c_master_bus_handle_t bus, uint8_t address);

/* 单次高重复度测量，无时钟拉伸；内部等待转换完成，需在任务中调用。
 * 成功输出摄氏温度和相对湿度（%RH）；通信或 CRC 错误时不修改输出。
 * 初始化、读取、释放不能对同一对象并发调用。 */
esp_err_t sht30_read(sht30_t *sensor, float *temperature_c, float *humidity_percent);

/* 仅释放传感器设备，不释放总线；失败时保留对象以便重试。 */
esp_err_t sht30_deinit(sht30_t *sensor);

#ifdef __cplusplus
}
#endif
