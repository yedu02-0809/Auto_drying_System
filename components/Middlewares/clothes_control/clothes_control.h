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

typedef struct {
    bool time_valid;
    uint32_t date_key;       /* Valid local YYYYMMDD, checked by the caller. */
    uint32_t seconds_of_day; /* 0..86399, in the same local timezone. */
    bool rain_valid;
    bool raining;
    clothes_command_t manual_command;
} clothes_inputs_t;

typedef struct {
    clothes_position_t position;
    clothes_command_t active_command;
    uint32_t last_evening_date;
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
