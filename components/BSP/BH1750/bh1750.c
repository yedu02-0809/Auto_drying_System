#include "bh1750.h"

#include <stddef.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define BH1750_I2C_SPEED_HZ 100000
#define BH1750_TIMEOUT_MS   100
#define BH1750_MEASURE_MS   180
#define BH1750_POWER_ON     0x01
#define BH1750_ONE_TIME_H   0x20

static esp_err_t send_command(bh1750_t *sensor, uint8_t command)
{
    return i2c_master_transmit(sensor->i2c_dev, &command, 1, BH1750_TIMEOUT_MS);
}

esp_err_t bh1750_init(bh1750_t *sensor, i2c_master_bus_handle_t bus, uint8_t address)
{
    if (sensor == NULL || bus == NULL ||
        (address != BH1750_I2C_ADDRESS_DEFAULT && address != BH1750_I2C_ADDRESS_ALTERNATE)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (sensor->i2c_dev != NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    sensor->initialized = false;
    const i2c_device_config_t config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = address,
        .scl_speed_hz = BH1750_I2C_SPEED_HZ,
    };
    i2c_master_dev_handle_t device = NULL;
    esp_err_t err = i2c_master_bus_add_device(bus, &config, &device);
    if (err != ESP_OK) {
        return err;
    }
    sensor->i2c_dev = device;

    /* Reset 只清测量数据且必须在 Power On 后使用，不会复位 MTreg。
     * MTreg=69(0b01000101)：高 3 位命令 0x42，低 5 位命令 0x65。 */
    const uint8_t commands[] = {BH1750_POWER_ON, 0x07, 0x42, 0x65, 0x00};
    for (size_t i = 0; i < sizeof(commands); ++i) {
        err = send_command(sensor, commands[i]);
        if (err != ESP_OK) {
            if (i2c_master_bus_rm_device(device) == ESP_OK) {
                sensor->i2c_dev = NULL;
            }
            return err;
        }
    }
    sensor->initialized = true;
    return ESP_OK;
}

esp_err_t bh1750_read_lux(bh1750_t *sensor, float *lux)
{
    if (sensor == NULL || lux == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (sensor->i2c_dev == NULL || !sensor->initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = send_command(sensor, BH1750_POWER_ON);
    if (err != ESP_OK) {
        return err;
    }
    err = send_command(sensor, BH1750_ONE_TIME_H);
    if (err != ESP_OK) {
        return err;
    }
    /* 高分辨率转换最长 180ms；加两个 tick 覆盖换算截断及调度 tick 边界。 */
    vTaskDelay(pdMS_TO_TICKS(BH1750_MEASURE_MS) + 2);
    uint8_t data[2];
    err = i2c_master_receive(sensor->i2c_dev, data, sizeof(data), BH1750_TIMEOUT_MS);
    if (err != ESP_OK) {
        return err;
    }
    const uint16_t raw = ((uint16_t)data[0] << 8) | data[1];
    /* MTreg=69，高分辨率模式（非 H-Resolution Mode2）的典型换算系数。 */
    *lux = raw / 1.2f;
    return ESP_OK;
}

esp_err_t bh1750_deinit(bh1750_t *sensor)
{
    if (sensor == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (sensor->i2c_dev == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = i2c_master_bus_rm_device(sensor->i2c_dev);
    if (err == ESP_OK) {
        sensor->i2c_dev = NULL;
        sensor->initialized = false;
    }
    return err;
}
