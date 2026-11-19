#pragma once

#include <stdbool.h>
#include "esp_err.h"
#include "clothes_control.h"
#include "web_status.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 启动 Wi-Fi STA 和局域网网页服务。非阻塞：只在失败时返回错误，
 * 连接过程和 HTTP 服务在后台任务里完成，不影响按键/雨滴/定时控制。
 * 未配置 SSID 时直接返回 ESP_ERR_INVALID_STATE，其余功能照常运行。 */
esp_err_t web_portal_start(void);

/* 由控制任务周期调用，更新网页看到的状态快照（带自旋锁保护）。 */
void web_portal_publish(const web_status_t *status);

/* 复制一份当前状态快照，供 OLED 等只读消费者使用。 */
void web_portal_snapshot(web_status_t *status);

/* 网页选择的模式，未启动网页服务时固定为 CLOTHES_MODE_AUTO。 */
clothes_mode_t web_portal_mode(void);

/* 非阻塞取出一条来自网页的手动命令；没有命令时返回 false。 */
bool web_portal_take_command(clothes_command_t *command);

#ifdef __cplusplus
}
#endif
