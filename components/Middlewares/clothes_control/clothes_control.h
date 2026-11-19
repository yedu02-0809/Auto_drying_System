#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum clothes_command_t {
    CLOTHES_CMD_NONE = 0,
    CLOTHES_CMD_EXTEND,
    CLOTHES_CMD_RETRACT,
} clothes_command_t;

/* Commanded position only: the SG90 provides no physical position feedback. */
typedef enum clothes_position_t {
    CLOTHES_POSITION_UNKNOWN = 0,
    CLOTHES_POSITION_EXTENDED,
    CLOTHES_POSITION_RETRACTED,
    CLOTHES_POSITION_MOVING_EXTEND,
    CLOTHES_POSITION_MOVING_RETRACT,
} clothes_position_t;

/* 自动模式运行 18:00 收回和白天自动推出规则；手动模式停用这两条定时规则。
 * 雨天收回是产品的安全功能，两种模式下都保持生效。 */
typedef enum clothes_mode_t {
    CLOTHES_MODE_AUTO = 0,
    CLOTHES_MODE_MANUAL,
} clothes_mode_t;

/* 自动推出规则：仅在 [08:00, 17:00) 窗口内、无雨、当前已收回，
 * 且光照连续 5 分钟不低于阈值时推出；每个日期最多自动推出一次。
 * 阈值需按实际安装位置和朝向标定，单位 lux。 */
#define CLOTHES_AUTO_EXTEND_START_SECONDS (8U * 60U * 60U)
#define CLOTHES_AUTO_EXTEND_END_SECONDS   (17U * 60U * 60U)
#define CLOTHES_AUTO_EXTEND_LUX           2000.0f
#define CLOTHES_AUTO_EXTEND_DWELL_SECONDS 300U

typedef struct {
    clothes_mode_t mode;
    bool time_valid;
    uint32_t date_key;       /* Valid local YYYYMMDD, checked by the caller. */
    uint32_t seconds_of_day; /* 0..86399, in the same local timezone. */
    bool rain_valid;
    bool raining;
    bool light_valid;        /* 最近一次 BH1750 采样成功且未过期。 */
    float lux;
    clothes_command_t manual_command;
    bool manual_toggle;     /* One debounced press; reverse current target, unknown -> retract. */
} clothes_inputs_t;

typedef struct {
    clothes_position_t position;
    clothes_command_t active_command;
    uint32_t last_evening_date;
    uint32_t last_extend_date;
    uint32_t bright_since_seconds; /* 0 = 当前光照不满足自动推出条件。 */
    uint32_t bright_since_date;    /* 累计计时所属日期，跨天必须重新累计。 */
    bool rain_known;
    bool raining;
} clothes_control_t;

void clothes_control_init(clothes_control_t *control);

/*
 * Returns a new movement command, or NONE if no motor update is needed.
 * The first valid reading at/after 18:00 consumes that day's automatic event,
 * even when already retracted. Manual extension afterwards is allowed if dry.
 * Dates older than the last consumed date cannot trigger again after rollback.
 * Rain/clock events are consumed even if subsequent motor movement fails;
 * retry then requires a new manual command or a new automatic event.
 * Daylight extension is limited to one attempt per date and only starts from
 * RETRACTED; manual mode disables both scheduled rules.
 */
clothes_command_t clothes_control_update(clothes_control_t *control,
                                         const clothes_inputs_t *inputs);

/* Only the currently active, non-NONE command can complete a movement. */
void clothes_control_complete(clothes_control_t *control, clothes_command_t command);

/* Stop tracking the failed movement; its physical position is unknown. */
void clothes_control_failed(clothes_control_t *control);

#ifdef __cplusplus
}
#endif
