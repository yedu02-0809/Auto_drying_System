#include "rtc_ds3231.h"

#include <stdbool.h>
#include <stddef.h>

#define RTC_I2C_ADDRESS       0x68
#define RTC_I2C_SPEED_HZ      100000
#define RTC_TIMEOUT_MS       100
#define RTC_REG_TIME         0x00
#define RTC_REG_CONTROL      0x0E
#define RTC_REG_STATUS       0x0F
#define RTC_EOSC             0x80
#define RTC_OSF              0x80

static bool datetime_valid(const rtc_datetime_t *datetime)
{
    static const uint8_t days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (datetime->year < 2000 || datetime->year > 2099 ||
        datetime->month < 1 || datetime->month > 12 ||
        datetime->hour > 23 || datetime->minute > 59 || datetime->second > 59) {
        return false;
    }
    uint8_t maximum = days[datetime->month - 1];
    if (datetime->month == 2 && datetime->year % 4 == 0) {
        ++maximum;  /* 2000 是闰年；2100 不在支持范围内。 */
    }
    return datetime->day >= 1 && datetime->day <= maximum;
}

static bool decode_bcd(uint8_t raw, uint8_t *value)
{
    if ((raw & 0x0F) > 9 || (raw >> 4) > 9) {
        return false;
    }
    *value = (raw >> 4) * 10 + (raw & 0x0F);
    return true;
}

static uint8_t encode_bcd(uint8_t value)
{
    return (uint8_t)(((value / 10) << 4) | (value % 10));
}

static esp_err_t read_registers(rtc_t *rtc, uint8_t address, uint8_t *data, size_t length)
{
    return i2c_master_transmit_receive(rtc->i2c_dev, &address, 1,
                                       data, length, RTC_TIMEOUT_MS);
}

esp_err_t rtc_ds3231_init(rtc_t *rtc, i2c_master_bus_handle_t bus)
{
    if (rtc == NULL || bus == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (rtc->i2c_dev != NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    const i2c_device_config_t config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = RTC_I2C_ADDRESS,
        .scl_speed_hz = RTC_I2C_SPEED_HZ,
    };
    i2c_master_dev_handle_t device = NULL;
    esp_err_t err = i2c_master_bus_add_device(bus, &config, &device);
    if (err != ESP_OK) {
        return err;
    }
    rtc->i2c_dev = device;
    uint8_t control;
    err = read_registers(rtc, RTC_REG_CONTROL, &control, 1);
    if (err == ESP_OK && (control & RTC_EOSC) != 0) {
        const uint8_t data[] = {RTC_REG_CONTROL, (uint8_t)(control & ~RTC_EOSC)};
        err = i2c_master_transmit(device, data, sizeof(data), RTC_TIMEOUT_MS);
    }
    if (err != ESP_OK && i2c_master_bus_rm_device(device) == ESP_OK) {
        rtc->i2c_dev = NULL;
    }
    return err;
}

esp_err_t rtc_ds3231_get_datetime(rtc_t *rtc, rtc_datetime_t *datetime)
{
    if (rtc == NULL || datetime == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (rtc->i2c_dev == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    /* DS3231 在 START 时锁存整组时间；一次连续读取避免秒进位造成日期拼接。 */
    uint8_t data[7];
    esp_err_t err = read_registers(rtc, RTC_REG_TIME, data, sizeof(data));
    if (err != ESP_OK) {
        return err;
    }
    uint8_t status;
    err = read_registers(rtc, RTC_REG_STATUS, &status, 1);
    if (err != ESP_OK) {
        return err;
    }
    if ((status & RTC_OSF) != 0) {
        return ESP_ERR_INVALID_STATE;
    }

    /* 验证保留位和世纪位，不把损坏数据掩码成看起来合法的时间。 */
    if ((data[0] & 0x80) || (data[1] & 0x80) || (data[2] & 0x80) ||
        data[3] < 1 || data[3] > 7 || (data[4] & 0xC0) || (data[5] & 0xE0)) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    rtc_datetime_t result = {0};
    uint8_t year;
    uint8_t hour = data[2] & ((data[2] & 0x40) ? 0x1F : 0x3F);
    if (!decode_bcd(data[0], &result.second) ||
        !decode_bcd(data[1], &result.minute) || !decode_bcd(hour, &result.hour) ||
        !decode_bcd(data[4], &result.day) || !decode_bcd(data[5], &result.month) ||
        !decode_bcd(data[6], &year)) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    if ((data[2] & 0x40) != 0) {
        if (result.hour < 1 || result.hour > 12) {
            return ESP_ERR_INVALID_RESPONSE;
        }
        result.hour = (result.hour % 12) + ((data[2] & 0x20) ? 12 : 0);
    }
    result.year = 2000 + year;
    if (!datetime_valid(&result)) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    *datetime = result;
    return ESP_OK;
}

esp_err_t rtc_ds3231_set_datetime(rtc_t *rtc, const rtc_datetime_t *datetime)
{
    if (rtc == NULL || datetime == NULL || !datetime_valid(datetime)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (rtc->i2c_dev == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    /* 公历星期计算，周日=1；只写时间区，不改变闹钟和输出控制配置。 */
    static const uint8_t month_offsets[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    unsigned year = datetime->year - (datetime->month < 3);
    uint8_t weekday = (year + year / 4 - year / 100 + year / 400 +
                       month_offsets[datetime->month - 1] + datetime->day) % 7 + 1;
    const uint8_t data[] = {
        RTC_REG_TIME, encode_bcd(datetime->second), encode_bcd(datetime->minute),
        encode_bcd(datetime->hour), weekday, encode_bcd(datetime->day),
        encode_bcd(datetime->month), encode_bcd(datetime->year - 2000),
    };
    esp_err_t err = i2c_master_transmit(rtc->i2c_dev, data, sizeof(data), RTC_TIMEOUT_MS);
    if (err != ESP_OK) {
        return err;
    }
    uint8_t status;
    err = read_registers(rtc, RTC_REG_STATUS, &status, 1);
    if (err != ESP_OK) {
        return err;
    }
    const uint8_t clear_osf[] = {RTC_REG_STATUS, (uint8_t)(status & ~RTC_OSF)};
    return i2c_master_transmit(rtc->i2c_dev, clear_osf, sizeof(clear_osf), RTC_TIMEOUT_MS);
}

esp_err_t rtc_ds3231_deinit(rtc_t *rtc)
{
    if (rtc == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (rtc->i2c_dev == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = i2c_master_bus_rm_device(rtc->i2c_dev);
    if (err == ESP_OK) {
        rtc->i2c_dev = NULL;
    }
    return err;
}
