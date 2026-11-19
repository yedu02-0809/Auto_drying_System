#include "display.h"

#include <stdio.h>

/* 各行字段来自 uint8_t 或已限幅的浮点值，实际宽度远小于 GCC 按
 * unsigned int / double 上限估算的最坏情况；真正的截断由 snprintf 保证，
 * 因此这里关闭该误报（GCC 7 起才有这个警告，旧版编译器会拒绝未知的 pragma）。 */
#if defined(__GNUC__) && !defined(__clang__) && (__GNUC__ >= 7)
#pragma GCC diagnostic ignored "-Wformat-truncation"
#endif

static const char *mode_text(int mode)
{
    return mode == 1 ? "MAN" : "AUTO";
}

static const char *position_text(int position)
{
    switch (position) {
    case 1: return "EXTEND";
    case 2: return "RETRACT";
    case 3: return "MOV:EXT";
    case 4: return "MOV:RET";
    default: return "UNKNOWN";
    }
}

static void format_lux(const web_status_t *status, char *buffer, size_t size)
{
    if (!status->light_valid || !(status->lux >= 0.0f)) {
        snprintf(buffer, size, "----lx");
        return;
    }
    unsigned long rounded = (unsigned long)(status->lux + 0.5f);
    if (rounded > 99999UL) {
        rounded = 99999UL;
    }
    snprintf(buffer, size, "%lulx", rounded);
}

void display_compose(const web_status_t *status,
                     char lines[DISPLAY_LINES][DISPLAY_COLUMNS + 1])
{
    if (status == NULL || lines == NULL) {
        return;
    }

    if (status->time_valid) {
        snprintf(lines[0], DISPLAY_COLUMNS + 1, "%02u-%02u %02u:%02u %s",
                 status->month, status->day, status->hour, status->minute,
                 mode_text(status->mode));
    } else {
        snprintf(lines[0], DISPLAY_COLUMNS + 1, "NO RTC  %s", mode_text(status->mode));
    }

    if (status->climate_valid) {
        snprintf(lines[1], DISPLAY_COLUMNS + 1, "%4.1fC %4.1f%%RH",
                 (double)status->temperature_c, (double)status->humidity_percent);
    } else {
        snprintf(lines[1], DISPLAY_COLUMNS + 1, "NO CLIMATE DATA");
    }

    char lux_text[16];
    format_lux(status, lux_text, sizeof(lux_text));
    const char *rain_text = "RAIN:--";
    if (status->rain_valid) {
        rain_text = status->raining ? "RAIN:YES" : "RAIN:NO";
    }
    snprintf(lines[2], DISPLAY_COLUMNS + 1, "%s %s", lux_text, rain_text);

    snprintf(lines[3], DISPLAY_COLUMNS + 1, "RACK:%s%s",
             position_text(status->position), status->wifi_connected ? " W" : "");
}

esp_err_t display_render(ssd1306_t *display, const web_status_t *status)
{
    if (display == NULL || status == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    char lines[DISPLAY_LINES][DISPLAY_COLUMNS + 1];
    display_compose(status, lines);

    esp_err_t err = ssd1306_clear(display);
    if (err != ESP_OK) {
        return err;
    }
    for (int line = 0; line < DISPLAY_LINES; ++line) {
        err = ssd1306_draw_text(display, line, 0, lines[line]);
        if (err != ESP_OK) {
            return err;
        }
    }
    return ssd1306_flush(display);
}
