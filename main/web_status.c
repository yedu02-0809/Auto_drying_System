#include "web_status.h"

#include <stdio.h>
#include <string.h>

static const char *json_bool(bool value)
{
    return value ? "true" : "false";
}

static const char *position_name(int position)
{
    /* 与 clothes_position_t 的取值一一对应，避免本模块依赖策略头文件。 */
    switch (position) {
    case 1: return "extended";
    case 2: return "retracted";
    case 3: return "moving_extend";
    case 4: return "moving_retract";
    default: return "unknown";
    }
}

static const char *mode_name(int mode)
{
    return mode == 1 ? "manual" : "auto";
}

size_t web_status_json(const web_status_t *status, char *buffer, size_t size)
{
    if (status == NULL || buffer == NULL || size == 0) {
        return 0;
    }
    const int written = snprintf(
        buffer, size,
        "{\"time\":\"%04u-%02u-%02u %02u:%02u:%02u\",\"time_valid\":%s,"
        "\"rain\":%s,\"rain_valid\":%s,"
        "\"temperature_c\":%.1f,\"humidity_percent\":%.1f,\"climate_valid\":%s,"
        "\"lux\":%.1f,\"light_valid\":%s,"
        "\"position\":\"%s\",\"mode\":\"%s\","
        "\"wifi_connected\":%s,\"ip\":\"%s\"}",
        status->year, status->month, status->day,
        status->hour, status->minute, status->second,
        json_bool(status->time_valid),
        json_bool(status->raining), json_bool(status->rain_valid),
        (double)status->temperature_c, (double)status->humidity_percent,
        json_bool(status->climate_valid),
        (double)status->lux, json_bool(status->light_valid),
        position_name(status->position), mode_name(status->mode),
        json_bool(status->wifi_connected), status->ip);
    (void)written;
    /* 不依赖 snprintf 的返回值语义（个别 C 库在截断时返回 -1）：
     * 强制补 NUL 后用 strlen 得到实际写入长度。 */
    buffer[size - 1] = '\0';
    return strlen(buffer);
}
