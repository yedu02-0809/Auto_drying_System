#include "clothes_control.h"

#include <stddef.h>

#define EVENING_SECONDS (18U * 60U * 60U)
#define DAY_SECONDS (24U * 60U * 60U)

void clothes_control_init(clothes_control_t *control)
{
    if (control != NULL) {
        *control = (clothes_control_t){
            .position = CLOTHES_POSITION_UNKNOWN,
            .active_command = CLOTHES_CMD_NONE,
        };
    }
}

clothes_command_t clothes_control_update(clothes_control_t *control,
                                         const clothes_inputs_t *inputs)
{
    if (control == NULL || inputs == NULL) {
        return CLOTHES_CMD_NONE;
    }

    bool rain_started = false;
    if (inputs->rain_valid) {
        rain_started = inputs->raining && (!control->rain_known || !control->raining);
        control->rain_known = true;
        control->raining = inputs->raining;
    }

    bool evening_due = inputs->time_valid &&
                       inputs->seconds_of_day >= EVENING_SECONDS &&
                       inputs->seconds_of_day < DAY_SECONDS &&
                       inputs->date_key > control->last_evening_date;
    if (evening_due) {
        control->last_evening_date = inputs->date_key;
    }

    clothes_command_t manual_command = inputs->manual_command;
    if (manual_command == CLOTHES_CMD_NONE && inputs->manual_toggle) {
        /* 自动收回与手动按键共用位置，避免独立按键计数与实际动作脱节。 */
        manual_command = control->position == CLOTHES_POSITION_RETRACTED ||
                         control->position == CLOTHES_POSITION_MOVING_RETRACT
                             ? CLOTHES_CMD_EXTEND : CLOTHES_CMD_RETRACT;
    }

    clothes_command_t requested = CLOTHES_CMD_NONE;
    if (manual_command == CLOTHES_CMD_RETRACT || rain_started || evening_due) {
        requested = CLOTHES_CMD_RETRACT;
    } else if (manual_command == CLOTHES_CMD_EXTEND && !control->raining) {
        requested = CLOTHES_CMD_EXTEND;
    }

    if (requested == CLOTHES_CMD_NONE || requested == control->active_command ||
        (requested == CLOTHES_CMD_RETRACT && control->position == CLOTHES_POSITION_RETRACTED) ||
        (requested == CLOTHES_CMD_EXTEND && control->position == CLOTHES_POSITION_EXTENDED)) {
        return CLOTHES_CMD_NONE;
    }

    control->active_command = requested;
    control->position = requested == CLOTHES_CMD_RETRACT
                            ? CLOTHES_POSITION_MOVING_RETRACT
                            : CLOTHES_POSITION_MOVING_EXTEND;
    return requested;
}

void clothes_control_complete(clothes_control_t *control, clothes_command_t command)
{
    if (control == NULL || command == CLOTHES_CMD_NONE ||
        command != control->active_command) {
        return;
    }
    control->position = command == CLOTHES_CMD_RETRACT
                            ? CLOTHES_POSITION_RETRACTED
                            : CLOTHES_POSITION_EXTENDED;
    control->active_command = CLOTHES_CMD_NONE;
}

void clothes_control_failed(clothes_control_t *control)
{
    if (control != NULL) {
        control->position = CLOTHES_POSITION_UNKNOWN;
        control->active_command = CLOTHES_CMD_NONE;
    }
}
