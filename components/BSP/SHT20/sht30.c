#include "sht30.h"

#include <stddef.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define SHT30_I2C_SPEED_HZ       100000
#define SHT30_TIMEOUT_MS        100
#define SHT30_WAIT_MS           20

static void wait_for_sensor(void)
{
    /* 向下换算后增加两个 tick，覆盖取整及调度 tick 边界，至少等 20ms。
     * 高重复度转换最长 15.5ms；上电和软复位也使用此保守等待时间。 */
    vTaskDelay(pdMS_TO_TICKS(SHT30_WAIT_MS) + 2);
}

static uint8_t crc8(const uint8_t *data, size_t length)
{
    uint8_t crc = 0xFF;
    for (size_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (unsigned int bit = 0; bit < 8; ++bit) {
            crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x31)
                               : (uint8_t)(crc << 1);
        }
    }
    return crc;
}

esp_err_t sht30_init(sht30_t *sensor, i2c_master_bus_handle_t bus, uint8_t address)
{
    if (sensor == NULL || bus == NULL ||
        (address != SHT30_I2C_ADDRESS_DEFAULT && address != SHT30_I2C_ADDRESS_ALTERNATE)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (sensor->i2c_dev != NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    sensor->initialized = false;
    const i2c_device_config_t config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = address,
        .scl_speed_hz = SHT30_I2C_SPEED_HZ,
    };
    i2c_master_dev_handle_t device = NULL;
    esp_err_t err = i2c_master_bus_add_device(bus, &config, &device);
    if (err != ESP_OK) {
        return err;
    }
    sensor->i2c_dev = device;

    wait_for_sensor();
    const uint8_t reset_command[] = {0x30, 0xA2};
    err = i2c_master_transmit(device, reset_command, sizeof(reset_command), SHT30_TIMEOUT_MS);
    if (err != ESP_OK) {
        if (i2c_master_bus_rm_device(device) == ESP_OK) {
            sensor->i2c_dev = NULL;
        }
        return err;
    }
    wait_for_sensor();
    sensor->initialized = true;
    return ESP_OK;
}

esp_err_t sht30_read(sht30_t *sensor, float *temperature_c, float *humidity_percent)
{
    if (sensor == NULL || temperature_c == NULL || humidity_percent == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (sensor->i2c_dev == NULL || !sensor->initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    /* 0x2400：单次测量、高重复度、禁用 clock stretching。
     * 写命令后先等待，不能立即使用 transmit_receive 读取。 */
    const uint8_t measure_command[] = {0x24, 0x00};
    esp_err_t err = i2c_master_transmit(sensor->i2c_dev, measure_command,
                                    sizeof(measure_command), SHT30_TIMEOUT_MS);
    if (err != ESP_OK) {
        return err;
    }
    wait_for_sensor();

    /* 顺序：温度 MSB、LSB、CRC，湿度 MSB、LSB、CRC。 */
    uint8_t data[6];
    err = i2c_master_receive(sensor->i2c_dev, data, sizeof(data), SHT30_TIMEOUT_MS);
    if (err != ESP_OK) {
        return err;
    }
    if (crc8(data, 2) != data[2] || crc8(data + 3, 2) != data[5]) {
        return ESP_ERR_INVALID_CRC;
    }

    const uint16_t raw_temperature = ((uint16_t)data[0] << 8) | data[1];
    const uint16_t raw_humidity = ((uint16_t)data[3] << 8) | data[4];
    *temperature_c = -45.0f + 175.0f * raw_temperature / 65535.0f;
    *humidity_percent = 100.0f * raw_humidity / 65535.0f;
    return ESP_OK;
}

esp_err_t sht30_deinit(sht30_t *sensor)
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
