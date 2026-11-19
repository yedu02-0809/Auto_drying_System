#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 与硬件无关的状态快照：网页 JSON 只依赖这个结构，便于主机测试。 */
typedef struct {
    bool time_valid;
    unsigned int year;
    unsigned int month;
    unsigned int day;
    unsigned int hour;
    unsigned int minute;
    unsigned int second;

    bool rain_valid;
    bool raining;

    bool climate_valid;
    float temperature_c;
    float humidity_percent;

    bool light_valid;
    float lux;

    /* 取值来自 clothes_position_t / clothes_mode_t，避免本模块依赖策略头文件。 */
    int position;
    int mode;

    bool wifi_connected;
    char ip[16];
} web_status_t;

/* 把状态写成一行 JSON。返回实际写入的字节数（不含结尾 NUL）；
 * 参数非法返回 0。缓冲区不足时输出被截断并返回 size-1，仍以 NUL 结尾，
 * 调用方可用 512 字节。位置和模式使用固定字符串，未知取值输出 "unknown"。 */
size_t web_status_json(const web_status_t *status, char *buffer, size_t size);

#ifdef __cplusplus
}
#endif
