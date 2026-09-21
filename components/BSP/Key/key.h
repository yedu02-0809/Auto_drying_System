#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "driver/gpio.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    KEY_EVENT_NONE = 0,
    KEY_EVENT_EXTEND,
    KEY_EVENT_RETRACT,
} key_event_t;

/* 以下字段由驱动维护，应用层只需将 key_pair_t 初始化为 {0}。 */
typedef struct {
    gpio_num_t gpio;
    bool candidate_pressed;
    bool stable_pressed;
    bool armed;
    int64_t changed_at_us;
} key_button_state_t;

typedef struct {
    key_button_state_t extend;
    key_button_state_t retract;
    bool initialized;
} key_pair_t;

/* 两个独立常开按键：GPIO -> 按键 -> GND，内部上拉，低电平按下。
 * 当前 ESP32 要求引脚支持内部上拉，故不接受 GPIO34~39。
 * 两个 GPIO 必须不同，首次调用前 keys 必须初始化为 {0}。 */
esp_err_t key_init(key_pair_t *keys, gpio_num_t extend_gpio, gpio_num_t retract_gpio);

/* 在同一任务中约每 10ms 调用一次；驱动不阻塞，连续稳定 30ms 后确认。
 * 每次按下只报告一次事件，松开后才可再次触发；开机已按住的键须先松开。
 * 两键同时按下时收回优先；收回保持时不接受伸出命令，也不缓存该命令。
 * 成功时 event 可为 KEY_EVENT_NONE；失败时不修改 event。 */
esp_err_t key_poll(key_pair_t *keys, key_event_t *event);

#ifdef __cplusplus
}
#endif
