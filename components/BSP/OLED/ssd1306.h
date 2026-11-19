#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "driver/i2c_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 0.96 寸 128x64 I2C OLED（SSD1306）。模块背面通常标 GND/VCC/SCL/SDA，
 * 7 位地址 0x3C（0x78 是 8 位写地址），部分模块是 0x3D。 */
#define SSD1306_WIDTH  128
#define SSD1306_HEIGHT 64
#define SSD1306_PAGES  (SSD1306_HEIGHT / 8)
#define SSD1306_ADDRESS_DEFAULT   0x3C
#define SSD1306_ADDRESS_ALTERNATE 0x3D

/* 8x16 字模下每行 16 个字符、共 4 行。 */
#define SSD1306_TEXT_COLUMNS 16
#define SSD1306_TEXT_LINES   4

/* 对象约 1 KB（含整屏显存），建议放在文件作用域或静态区，不要放任务栈。 */
typedef struct {
    i2c_master_dev_handle_t i2c_dev;
    bool initialized;
    uint8_t buffer[SSD1306_WIDTH * SSD1306_PAGES]; /* 1 = 点亮 */
} ssd1306_t;

/* 在调用者的共享 I2C 总线上添加设备并下发初始化序列，随后清屏并刷新一次。
 * 失败时会尝试删除设备；若删除也失败，保留句柄供 ssd1306_deinit() 重试。
 * 不会释放调用者的总线，可与 SHT30 / DS3231 / BH1750 共用 I2C0。 */
esp_err_t ssd1306_init(ssd1306_t *display, i2c_master_bus_handle_t bus, uint8_t address);

/* 只操作显存，不访问 I2C。line 0..3，column 0..15；超出 16 列的字符被截断，
 * 非可打印字符显示为 '?'。文本要调用 ssd1306_flush() 才会出现在屏幕上。 */
esp_err_t ssd1306_clear(ssd1306_t *display);
esp_err_t ssd1306_draw_text(ssd1306_t *display, int line, int column, const char *text);

/* 把整屏显存按页写入屏幕，共 8 次 128 字节的数据传输，需要设备已初始化。 */
esp_err_t ssd1306_flush(ssd1306_t *display);

/* 仅删除设备，不释放共享总线；失败时保留对象以便重试。 */
esp_err_t ssd1306_deinit(ssd1306_t *display);

#ifdef __cplusplus
}
#endif
