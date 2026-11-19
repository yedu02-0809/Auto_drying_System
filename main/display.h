#pragma once

#include "esp_err.h"
#include "ssd1306.h"
#include "web_status.h"

#ifdef __cplusplus
extern "C" {
#endif

/* OLED 首页固定 4 行 × 16 列（8x16 字模）。状态快照复用 web_status_t：
 * 网页 JSON 和 OLED 渲染共用同一份字段，避免两套结构互相漂移。 */
#define DISPLAY_LINES   4
#define DISPLAY_COLUMNS 16

/* 把状态格式化成 4 行文本，纯函数、不访问硬件，便于主机测试。 */
void display_compose(const web_status_t *status,
                     char lines[DISPLAY_LINES][DISPLAY_COLUMNS + 1]);

/* 清屏 -> 写入 4 行 -> 刷新。失败通常表示 OLED 掉线或总线异常。 */
esp_err_t display_render(ssd1306_t *display, const web_status_t *status);

#ifdef __cplusplus
}
#endif
