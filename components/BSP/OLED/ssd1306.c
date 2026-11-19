#include "ssd1306.h"

#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ssd1306_font.h"

static const char *TAG = "ssd1306";

#define SSD1306_I2C_TIMEOUT_MS 100
#define SSD1306_SCL_SPEED_HZ   400000
#define SSD1306_CONTROL_COMMAND 0x00
#define SSD1306_CONTROL_DATA    0x40
#define SSD1306_POWER_ON_DELAY_MS 100

/* 与 STM32 工程相同的 SSD1306 初始化序列：
 * 显示关闭 -> 时钟分频 -> 复用率 64 -> 无偏移 -> 起始行 0 -> 段/COM 方向
 * -> COM 引脚配置 -> 对比度 -> 预充电 -> VCOMH -> 正常显示 -> 开电荷泵 -> 开显示。
 * 电荷泵必须在 0xAF 之前打开，否则 3.3V 供电下屏幕不亮。 */
static const uint8_t INIT_SEQUENCE[] = {
    0xAE,
    0xD5, 0x80,
    0xA8, 0x3F,
    0xD3, 0x00,
    0x40,
    0xA1,
    0xC8,
    0xDA, 0x12,
    0x81, 0xCF,
    0xD9, 0xF1,
    0xDB, 0x30,
    0xA4,
    0xA6,
    0x8D, 0x14,
    0xAF,
};

static esp_err_t write_commands(ssd1306_t *display, const uint8_t *commands, size_t count)
{
    uint8_t tx[1 + sizeof(INIT_SEQUENCE)];
    if (count > sizeof(tx) - 1) {
        return ESP_ERR_INVALID_SIZE;
    }
    tx[0] = SSD1306_CONTROL_COMMAND;
    memcpy(&tx[1], commands, count);
    return i2c_master_transmit(display->i2c_dev, tx, count + 1, SSD1306_I2C_TIMEOUT_MS);
}

static esp_err_t write_page(ssd1306_t *display, uint8_t page)
{
    const uint8_t page_commands[] = {
        (uint8_t)(0xB0 | page), /* 设置页起始地址 */
        0x00,                   /* 列低 4 位 = 0 */
        0x10,                   /* 列高 4 位 = 0 */
    };
    esp_err_t err = write_commands(display, page_commands, sizeof(page_commands));
    if (err != ESP_OK) {
        return err;
    }
    uint8_t tx[1 + SSD1306_WIDTH];
    tx[0] = SSD1306_CONTROL_DATA;
    memcpy(&tx[1], &display->buffer[page * SSD1306_WIDTH], SSD1306_WIDTH);
    return i2c_master_transmit(display->i2c_dev, tx, sizeof(tx), SSD1306_I2C_TIMEOUT_MS);
}

esp_err_t ssd1306_init(ssd1306_t *display, i2c_master_bus_handle_t bus, uint8_t address)
{
    if (display == NULL || bus == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (display->initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (address != SSD1306_ADDRESS_DEFAULT && address != SSD1306_ADDRESS_ALTERNATE) {
        return ESP_ERR_INVALID_ARG;
    }
    /* 上一次 deinit 失败会留下句柄，先清理再重新添加设备。 */
    if (display->i2c_dev != NULL) {
        const esp_err_t cleanup = ssd1306_deinit(display);
        if (cleanup != ESP_OK) {
            return cleanup;
        }
    }

    const i2c_device_config_t config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = address,
        .scl_speed_hz = SSD1306_SCL_SPEED_HZ,
    };
    esp_err_t err = i2c_master_bus_add_device(bus, &config, &display->i2c_dev);
    if (err != ESP_OK) {
        display->i2c_dev = NULL;
        return err;
    }

    /* OLED 上电后需要一段稳定时间，否则首条命令可能丢失。 */
    vTaskDelay(pdMS_TO_TICKS(SSD1306_POWER_ON_DELAY_MS));

    err = write_commands(display, INIT_SEQUENCE, sizeof(INIT_SEQUENCE));
    if (err == ESP_OK) {
        memset(display->buffer, 0, sizeof(display->buffer));
        display->initialized = true;
        err = ssd1306_flush(display);
    }
    if (err != ESP_OK) {
        display->initialized = false;
        ESP_LOGW(TAG, "OLED init failed: %s", esp_err_to_name(err));
        const esp_err_t cleanup = ssd1306_deinit(display);
        if (cleanup != ESP_OK) {
            ESP_LOGW(TAG, "OLED cleanup failed: %s; handle kept for retry",
                     esp_err_to_name(cleanup));
        }
        return err;
    }

    ESP_LOGI(TAG, "SSD1306 ready at 0x%02X", address);
    return ESP_OK;
}

esp_err_t ssd1306_clear(ssd1306_t *display)
{
    if (display == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(display->buffer, 0, sizeof(display->buffer));
    return ESP_OK;
}

esp_err_t ssd1306_draw_text(ssd1306_t *display, int line, int column, const char *text)
{
    if (display == NULL || text == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (line < 0 || line >= SSD1306_TEXT_LINES ||
        column < 0 || column >= SSD1306_TEXT_COLUMNS) {
        return ESP_ERR_INVALID_ARG;
    }

    const size_t glyphs = sizeof(ssd1306_font8x16) / sizeof(ssd1306_font8x16[0]);
    int current = column;
    for (const char *cursor = text; *cursor != '\0' && current < SSD1306_TEXT_COLUMNS; ++cursor) {
        unsigned int code = (unsigned char)*cursor;
        if (code < 0x20 || code > 0x7E) {
            code = '?';
        }
        if (code - 0x20 >= glyphs) {
            break;
        }
        const uint8_t *glyph = ssd1306_font8x16[code - 0x20];
        const int x = current * 8;
        uint8_t *upper = &display->buffer[(line * 2) * SSD1306_WIDTH + x];
        uint8_t *lower = &display->buffer[(line * 2 + 1) * SSD1306_WIDTH + x];
        memcpy(upper, glyph, 8);
        memcpy(lower, glyph + 8, 8);
        ++current;
    }
    return ESP_OK;
}

esp_err_t ssd1306_flush(ssd1306_t *display)
{
    if (display == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!display->initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    for (uint8_t page = 0; page < SSD1306_PAGES; ++page) {
        const esp_err_t err = write_page(display, page);
        if (err != ESP_OK) {
            return err;
        }
    }
    return ESP_OK;
}

esp_err_t ssd1306_deinit(ssd1306_t *display)
{
    if (display == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (display->i2c_dev == NULL) {
        display->initialized = false;
        return ESP_OK;
    }
    const esp_err_t err = i2c_master_bus_rm_device(display->i2c_dev);
    if (err != ESP_OK) {
        return err;
    }
    display->i2c_dev = NULL;
    display->initialized = false;
    return ESP_OK;
}
