/* Exercise production policy with user actions, clock changes and motor results. */
#include <math.h>
#include <stddef.h>
#include "clothes_control.h"

#define CHECK(condition) do { if (!(condition)) return __LINE__; } while (0)
#define EXPECT(command) CHECK(clothes_control_update(&control, &input) == (command))
#define FINISH(command) clothes_control_complete(&control, (command))
#define SETUP() \
    clothes_control_t control; \
    clothes_control_init(&control); \
    clothes_inputs_t input = {0}
#define CLOCK(date, seconds) do { \
    input.time_valid = true; input.date_key = (date); input.seconds_of_day = (seconds); \
} while (0)
#define EVENING 64800U
#define NOON (12U * 60U * 60U)
#define SET_LIGHT(value) do { input.light_valid = true; input.lux = (value); } while (0)
#define PRESS(command) do { \
    input.manual_toggle = true; EXPECT(command); input.manual_toggle = false; \
} while (0)

static int boot_position_is_unknown(void)
{
    SETUP();
    CHECK(control.position == CLOTHES_POSITION_UNKNOWN);
    CHECK(control.active_command == CLOTHES_CMD_NONE);
    EXPECT(CLOTHES_CMD_NONE);
    return 0;
}

static int six_pm_boundary_fires_once(void)
{
    SETUP();
    CLOCK(20260921, EVENING - 1);
    EXPECT(CLOTHES_CMD_NONE);
    input.seconds_of_day = EVENING;
    EXPECT(CLOTHES_CMD_RETRACT);
    CHECK(control.position == CLOTHES_POSITION_MOVING_RETRACT);
    CHECK(control.active_command == CLOTHES_CMD_RETRACT);
    EXPECT(CLOTHES_CMD_NONE);
    FINISH(CLOTHES_CMD_RETRACT);
    EXPECT(CLOTHES_CMD_NONE);
    CHECK(control.position == CLOTHES_POSITION_RETRACTED);
    return 0;
}

static int boot_after_six_pm_catches_up(void)
{
    SETUP();
    CLOCK(20260921, 86399);
    EXPECT(CLOTHES_CMD_RETRACT);
    return 0;
}

static int retracted_early_skips_evening(void)
{
    SETUP();
    input.manual_command = CLOTHES_CMD_RETRACT;
    EXPECT(CLOTHES_CMD_RETRACT);
    FINISH(CLOTHES_CMD_RETRACT);
    input.manual_command = CLOTHES_CMD_NONE;
    CLOCK(20260921, EVENING);
    EXPECT(CLOTHES_CMD_NONE);
    CHECK(control.last_evening_date == 20260921);
    CHECK(control.position == CLOTHES_POSITION_RETRACTED);
    return 0;
}

static int already_retracting_skips_evening(void)
{
    SETUP();
    input.manual_command = CLOTHES_CMD_RETRACT;
    EXPECT(CLOTHES_CMD_RETRACT);
    input.manual_command = CLOTHES_CMD_NONE;
    CLOCK(20260921, EVENING);
    EXPECT(CLOTHES_CMD_NONE);
    CHECK(control.position == CLOTHES_POSITION_MOVING_RETRACT);
    CHECK(control.last_evening_date == 20260921);
    return 0;
}

static int reextended_before_evening_is_retracted(void)
{
    SETUP();
    input.manual_command = CLOTHES_CMD_RETRACT;
    EXPECT(CLOTHES_CMD_RETRACT);
    FINISH(CLOTHES_CMD_RETRACT);
    input.manual_command = CLOTHES_CMD_EXTEND;
    EXPECT(CLOTHES_CMD_EXTEND);
    FINISH(CLOTHES_CMD_EXTEND);
    input.manual_command = CLOTHES_CMD_NONE;
    CLOCK(20260921, EVENING);
    EXPECT(CLOTHES_CMD_RETRACT);
    return 0;
}

static int evening_reverses_ongoing_extension(void)
{
    SETUP();
    input.manual_command = CLOTHES_CMD_EXTEND;
    EXPECT(CLOTHES_CMD_EXTEND);
    input.manual_command = CLOTHES_CMD_NONE;
    CLOCK(20260921, EVENING);
    EXPECT(CLOTHES_CMD_RETRACT);
    FINISH(CLOTHES_CMD_EXTEND);
    CHECK(control.position == CLOTHES_POSITION_MOVING_RETRACT);
    FINISH(CLOTHES_CMD_RETRACT);
    CHECK(control.position == CLOTHES_POSITION_RETRACTED);
    return 0;
}

static int manual_extension_after_evening_stays_extended(void)
{
    SETUP();
    CLOCK(20260921, EVENING);
    EXPECT(CLOTHES_CMD_RETRACT);
    FINISH(CLOTHES_CMD_RETRACT);
    input.manual_command = CLOTHES_CMD_EXTEND;
    EXPECT(CLOTHES_CMD_EXTEND);
    FINISH(CLOTHES_CMD_EXTEND);
    input.manual_command = CLOTHES_CMD_NONE;
    input.seconds_of_day = 86399;
    EXPECT(CLOTHES_CMD_NONE);
    CHECK(control.position == CLOTHES_POSITION_EXTENDED);
    return 0;
}

static int skipped_evening_is_still_consumed(void)
{
    SETUP();
    input.manual_command = CLOTHES_CMD_RETRACT;
    EXPECT(CLOTHES_CMD_RETRACT);
    FINISH(CLOTHES_CMD_RETRACT);
    CLOCK(20260921, EVENING);
    input.manual_command = CLOTHES_CMD_NONE;
    EXPECT(CLOTHES_CMD_NONE);
    input.manual_command = CLOTHES_CMD_EXTEND;
    EXPECT(CLOTHES_CMD_EXTEND);
    input.manual_command = CLOTHES_CMD_NONE;
    EXPECT(CLOTHES_CMD_NONE);
    return 0;
}

static int next_day_rearms_at_six_pm(void)
{
    SETUP();
    CLOCK(20260921, EVENING);
    EXPECT(CLOTHES_CMD_RETRACT);
    FINISH(CLOTHES_CMD_RETRACT);
    CLOCK(20260922, 0);
    EXPECT(CLOTHES_CMD_NONE);
    input.manual_command = CLOTHES_CMD_EXTEND;
    EXPECT(CLOTHES_CMD_EXTEND);
    FINISH(CLOTHES_CMD_EXTEND);
    input.manual_command = CLOTHES_CMD_NONE;
    input.seconds_of_day = EVENING - 1;
    EXPECT(CLOTHES_CMD_NONE);
    input.seconds_of_day = EVENING;
    EXPECT(CLOTHES_CMD_RETRACT);
    return 0;
}

static int year_rollover_rearms(void)
{
    SETUP();
    CLOCK(20261231, EVENING);
    EXPECT(CLOTHES_CMD_RETRACT);
    FINISH(CLOTHES_CMD_RETRACT);
    input.manual_command = CLOTHES_CMD_EXTEND;
    EXPECT(CLOTHES_CMD_EXTEND);
    FINISH(CLOTHES_CMD_EXTEND);
    input.manual_command = CLOTHES_CMD_NONE;
    CLOCK(20270101, EVENING);
    EXPECT(CLOTHES_CMD_RETRACT);
    return 0;
}

static int clock_rollback_cannot_replay_evening(void)
{
    SETUP();
    CLOCK(20260921, EVENING);
    EXPECT(CLOTHES_CMD_RETRACT);
    FINISH(CLOTHES_CMD_RETRACT);
    input.manual_command = CLOTHES_CMD_EXTEND;
    EXPECT(CLOTHES_CMD_EXTEND);
    FINISH(CLOTHES_CMD_EXTEND);
    input.manual_command = CLOTHES_CMD_NONE;
    CLOCK(20260920, EVENING);
    EXPECT(CLOTHES_CMD_NONE);
    CLOCK(20260921, EVENING - 1);
    EXPECT(CLOTHES_CMD_NONE);
    input.seconds_of_day = EVENING;
    EXPECT(CLOTHES_CMD_NONE);
    return 0;
}

static int invalid_clock_does_not_consume_event(void)
{
    SETUP();
    CLOCK(20260921, EVENING);
    input.time_valid = false;
    EXPECT(CLOTHES_CMD_NONE);
    CHECK(control.last_evening_date == 0);
    input.time_valid = true;
    EXPECT(CLOTHES_CMD_RETRACT);
    return 0;
}

static int invalid_seconds_and_zero_date_are_ignored(void)
{
    SETUP();
    CLOCK(20260921, 86400);
    EXPECT(CLOTHES_CMD_NONE);
    CLOCK(0, EVENING);
    EXPECT(CLOTHES_CMD_NONE);
    CHECK(control.last_evening_date == 0);
    return 0;
}

static int invalid_clock_keeps_buttons_and_rain_working(void)
{
    SETUP();
    input.manual_command = CLOTHES_CMD_EXTEND;
    EXPECT(CLOTHES_CMD_EXTEND);
    FINISH(CLOTHES_CMD_EXTEND);
    input.manual_command = CLOTHES_CMD_NONE;
    input.rain_valid = true;
    input.raining = true;
    EXPECT(CLOTHES_CMD_RETRACT);
    return 0;
}

static int first_rain_retracts_once(void)
{
    SETUP();
    input.rain_valid = true;
    input.raining = true;
    EXPECT(CLOTHES_CMD_RETRACT);
    EXPECT(CLOTHES_CMD_NONE);
    FINISH(CLOTHES_CMD_RETRACT);
    EXPECT(CLOTHES_CMD_NONE);
    return 0;
}

static int rain_early_skips_evening(void)
{
    SETUP();
    input.rain_valid = true;
    input.raining = true;
    EXPECT(CLOTHES_CMD_RETRACT);
    FINISH(CLOTHES_CMD_RETRACT);
    CLOCK(20260921, EVENING);
    EXPECT(CLOTHES_CMD_NONE);
    return 0;
}

static int rain_wins_over_manual_extend(void)
{
    SETUP();
    input.rain_valid = true;
    input.raining = true;
    input.manual_command = CLOTHES_CMD_EXTEND;
    EXPECT(CLOTHES_CMD_RETRACT);
    FINISH(CLOTHES_CMD_RETRACT);
    EXPECT(CLOTHES_CMD_NONE);
    CHECK(control.position == CLOTHES_POSITION_RETRACTED);
    return 0;
}

static int evening_wins_over_manual_extend(void)
{
    SETUP();
    CLOCK(20260921, EVENING);
    input.manual_command = CLOTHES_CMD_EXTEND;
    EXPECT(CLOTHES_CMD_RETRACT);
    return 0;
}

static int missing_rain_reading_preserves_wet_lockout(void)
{
    SETUP();
    input.rain_valid = true;
    input.raining = true;
    EXPECT(CLOTHES_CMD_RETRACT);
    FINISH(CLOTHES_CMD_RETRACT);
    input.rain_valid = false;
    input.raining = false;
    input.manual_command = CLOTHES_CMD_EXTEND;
    EXPECT(CLOTHES_CMD_NONE);
    CHECK(control.raining);
    return 0;
}

static int rain_stopping_does_not_extend(void)
{
    SETUP();
    input.rain_valid = true;
    input.raining = true;
    EXPECT(CLOTHES_CMD_RETRACT);
    FINISH(CLOTHES_CMD_RETRACT);
    input.raining = false;
    EXPECT(CLOTHES_CMD_NONE);
    CHECK(control.position == CLOTHES_POSITION_RETRACTED);
    input.manual_command = CLOTHES_CMD_EXTEND;
    EXPECT(CLOTHES_CMD_EXTEND);
    return 0;
}

static int later_rain_retracts_again(void)
{
    SETUP();
    input.rain_valid = true;
    input.raining = true;
    EXPECT(CLOTHES_CMD_RETRACT);
    FINISH(CLOTHES_CMD_RETRACT);
    input.raining = false;
    input.manual_command = CLOTHES_CMD_EXTEND;
    EXPECT(CLOTHES_CMD_EXTEND);
    FINISH(CLOTHES_CMD_EXTEND);
    input.manual_command = CLOTHES_CMD_NONE;
    input.raining = true;
    EXPECT(CLOTHES_CMD_RETRACT);
    return 0;
}

static int manual_retract_reverses_extension(void)
{
    SETUP();
    input.manual_command = CLOTHES_CMD_EXTEND;
    EXPECT(CLOTHES_CMD_EXTEND);
    input.manual_command = CLOTHES_CMD_RETRACT;
    EXPECT(CLOTHES_CMD_RETRACT);
    EXPECT(CLOTHES_CMD_NONE);
    FINISH(CLOTHES_CMD_RETRACT);
    EXPECT(CLOTHES_CMD_NONE);
    return 0;
}

static int dry_manual_extend_can_reverse_retraction(void)
{
    SETUP();
    input.manual_command = CLOTHES_CMD_RETRACT;
    EXPECT(CLOTHES_CMD_RETRACT);
    input.manual_command = CLOTHES_CMD_EXTEND;
    EXPECT(CLOTHES_CMD_EXTEND);
    EXPECT(CLOTHES_CMD_NONE);
    FINISH(CLOTHES_CMD_EXTEND);
    EXPECT(CLOTHES_CMD_NONE);
    return 0;
}

static int completion_requires_current_command(void)
{
    SETUP();
    FINISH(CLOTHES_CMD_RETRACT);
    FINISH(CLOTHES_CMD_NONE);
    CHECK(control.position == CLOTHES_POSITION_UNKNOWN);
    input.manual_command = CLOTHES_CMD_RETRACT;
    EXPECT(CLOTHES_CMD_RETRACT);
    FINISH(CLOTHES_CMD_NONE);
    FINISH(CLOTHES_CMD_EXTEND);
    CHECK(control.position == CLOTHES_POSITION_MOVING_RETRACT);
    FINISH(CLOTHES_CMD_RETRACT);
    CHECK(control.position == CLOTHES_POSITION_RETRACTED);
    CHECK(control.active_command == CLOTHES_CMD_NONE);
    return 0;
}

static int failed_evening_does_not_busy_retry(void)
{
    SETUP();
    CLOCK(20260921, EVENING);
    EXPECT(CLOTHES_CMD_RETRACT);
    clothes_control_failed(&control);
    CHECK(control.position == CLOTHES_POSITION_UNKNOWN);
    CHECK(control.active_command == CLOTHES_CMD_NONE);
    EXPECT(CLOTHES_CMD_NONE);
    FINISH(CLOTHES_CMD_RETRACT);
    CHECK(control.position == CLOTHES_POSITION_UNKNOWN);
    input.manual_command = CLOTHES_CMD_RETRACT;
    EXPECT(CLOTHES_CMD_RETRACT);
    return 0;
}

static int failed_rain_waits_for_new_event(void)
{
    SETUP();
    input.rain_valid = true;
    input.raining = true;
    EXPECT(CLOTHES_CMD_RETRACT);
    clothes_control_failed(&control);
    EXPECT(CLOTHES_CMD_NONE);
    input.manual_command = CLOTHES_CMD_EXTEND;
    EXPECT(CLOTHES_CMD_NONE);
    input.manual_command = CLOTHES_CMD_NONE;
    input.raining = false;
    EXPECT(CLOTHES_CMD_NONE);
    input.raining = true;
    EXPECT(CLOTHES_CMD_RETRACT);
    return 0;
}

static int next_day_can_retry_failed_evening(void)
{
    SETUP();
    CLOCK(20260921, EVENING);
    EXPECT(CLOTHES_CMD_RETRACT);
    clothes_control_failed(&control);
    CLOCK(20260922, EVENING);
    EXPECT(CLOTHES_CMD_RETRACT);
    return 0;
}

static int simultaneous_events_produce_one_command(void)
{
    SETUP();
    CLOCK(20260921, EVENING);
    input.rain_valid = true;
    input.raining = true;
    input.manual_command = CLOTHES_CMD_RETRACT;
    EXPECT(CLOTHES_CMD_RETRACT);
    clothes_control_failed(&control);
    input.manual_command = CLOTHES_CMD_NONE;
    EXPECT(CLOTHES_CMD_NONE);
    return 0;
}

static int null_arguments_are_safe(void)
{
    SETUP();
    clothes_control_init(NULL);
    CHECK(clothes_control_update(NULL, &input) == CLOTHES_CMD_NONE);
    CHECK(clothes_control_update(&control, NULL) == CLOTHES_CMD_NONE);
    clothes_control_complete(NULL, CLOTHES_CMD_RETRACT);
    clothes_control_failed(NULL);
    CHECK(control.position == CLOTHES_POSITION_UNKNOWN);
    return 0;
}

static int first_toggle_retracts_unknown_position(void)
{
    SETUP();
    PRESS(CLOTHES_CMD_RETRACT);
    CHECK(control.position == CLOTHES_POSITION_MOVING_RETRACT);
    CHECK(control.active_command == CLOTHES_CMD_RETRACT);
    EXPECT(CLOTHES_CMD_NONE);
    FINISH(CLOTHES_CMD_RETRACT);
    EXPECT(CLOTHES_CMD_NONE);
    return 0;
}

static int successive_toggles_alternate_completed_positions(void)
{
    SETUP();
    PRESS(CLOTHES_CMD_RETRACT);
    FINISH(CLOTHES_CMD_RETRACT);
    PRESS(CLOTHES_CMD_EXTEND);
    FINISH(CLOTHES_CMD_EXTEND);
    CHECK(control.position == CLOTHES_POSITION_EXTENDED);
    EXPECT(CLOTHES_CMD_NONE);
    PRESS(CLOTHES_CMD_RETRACT);
    FINISH(CLOTHES_CMD_RETRACT);
    CHECK(control.position == CLOTHES_POSITION_RETRACTED);
    PRESS(CLOTHES_CMD_EXTEND);
    return 0;
}

static int toggle_reverses_in_both_moving_states(void)
{
    SETUP();
    PRESS(CLOTHES_CMD_RETRACT);
    PRESS(CLOTHES_CMD_EXTEND);
    CHECK(control.position == CLOTHES_POSITION_MOVING_EXTEND);
    FINISH(CLOTHES_CMD_RETRACT);
    CHECK(control.position == CLOTHES_POSITION_MOVING_EXTEND);
    PRESS(CLOTHES_CMD_RETRACT);
    CHECK(control.position == CLOTHES_POSITION_MOVING_RETRACT);
    FINISH(CLOTHES_CMD_EXTEND);
    CHECK(control.position == CLOTHES_POSITION_MOVING_RETRACT);
    FINISH(CLOTHES_CMD_RETRACT);
    return 0;
}

static int failed_movement_next_toggle_retries_retraction(void)
{
    SETUP();
    PRESS(CLOTHES_CMD_RETRACT);
    clothes_control_failed(&control);
    CHECK(control.position == CLOTHES_POSITION_UNKNOWN);
    EXPECT(CLOTHES_CMD_NONE);
    PRESS(CLOTHES_CMD_RETRACT);
    FINISH(CLOTHES_CMD_RETRACT);
    PRESS(CLOTHES_CMD_EXTEND);
    clothes_control_failed(&control);
    CHECK(control.position == CLOTHES_POSITION_UNKNOWN);
    EXPECT(CLOTHES_CMD_NONE);
    PRESS(CLOTHES_CMD_RETRACT);
    return 0;
}

static int automatic_rain_retraction_updates_next_toggle(void)
{
    SETUP();
    PRESS(CLOTHES_CMD_RETRACT);
    FINISH(CLOTHES_CMD_RETRACT);
    PRESS(CLOTHES_CMD_EXTEND);
    FINISH(CLOTHES_CMD_EXTEND);
    input.rain_valid = true;
    input.raining = true;
    EXPECT(CLOTHES_CMD_RETRACT);
    FINISH(CLOTHES_CMD_RETRACT);
    input.raining = false;
    EXPECT(CLOTHES_CMD_NONE);
    PRESS(CLOTHES_CMD_EXTEND);
    CHECK(control.position == CLOTHES_POSITION_MOVING_EXTEND);
    return 0;
}

static int wet_blocked_toggles_do_not_change_next_dry_action(void)
{
    SETUP();
    input.rain_valid = true;
    input.raining = true;
    EXPECT(CLOTHES_CMD_RETRACT);
    PRESS(CLOTHES_CMD_NONE);
    CHECK(control.position == CLOTHES_POSITION_MOVING_RETRACT);
    FINISH(CLOTHES_CMD_RETRACT);
    PRESS(CLOTHES_CMD_NONE);
    PRESS(CLOTHES_CMD_NONE);
    CHECK(control.position == CLOTHES_POSITION_RETRACTED);
    CHECK(control.active_command == CLOTHES_CMD_NONE);
    input.rain_valid = false;
    input.raining = false;
    PRESS(CLOTHES_CMD_NONE);
    CHECK(control.raining);
    input.rain_valid = true;
    EXPECT(CLOTHES_CMD_NONE);
    PRESS(CLOTHES_CMD_EXTEND);
    return 0;
}

static int rain_start_overrides_toggle_extension(void)
{
    SETUP();
    PRESS(CLOTHES_CMD_RETRACT);
    input.rain_valid = true;
    input.raining = true;
    PRESS(CLOTHES_CMD_NONE);
    CHECK(control.position == CLOTHES_POSITION_MOVING_RETRACT);
    CHECK(control.active_command == CLOTHES_CMD_RETRACT);
    FINISH(CLOTHES_CMD_RETRACT);
    return 0;
}

static int evening_overrides_toggle_then_next_dry_press_extends(void)
{
    SETUP();
    PRESS(CLOTHES_CMD_RETRACT);
    FINISH(CLOTHES_CMD_RETRACT);
    CLOCK(20260924, EVENING);
    PRESS(CLOTHES_CMD_NONE);
    CHECK(control.position == CLOTHES_POSITION_RETRACTED);
    CHECK(control.last_evening_date == 20260924);
    PRESS(CLOTHES_CMD_EXTEND);
    FINISH(CLOTHES_CMD_EXTEND);
    EXPECT(CLOTHES_CMD_NONE);
    CLOCK(20260925, EVENING);
    EXPECT(CLOTHES_CMD_RETRACT);
    FINISH(CLOTHES_CMD_RETRACT);
    PRESS(CLOTHES_CMD_EXTEND);
    return 0;
}

static int toggle_retracted_early_skips_evening(void)
{
    SETUP();
    PRESS(CLOTHES_CMD_RETRACT);
    FINISH(CLOTHES_CMD_RETRACT);
    CLOCK(20260924, EVENING);
    EXPECT(CLOTHES_CMD_NONE);
    CHECK(control.position == CLOTHES_POSITION_RETRACTED);
    CHECK(control.last_evening_date == 20260924);
    return 0;
}

/* ---- 白天按光照自动推出 ---- */

static int daylight_extend_requires_full_dwell(void)
{
    SETUP();
    control.position = CLOTHES_POSITION_RETRACTED;
    CLOCK(20260501, NOON);
    SET_LIGHT(5000.0f);
    EXPECT(CLOTHES_CMD_NONE);
    CHECK(control.bright_since_seconds == NOON);
    input.seconds_of_day = NOON + 299;
    EXPECT(CLOTHES_CMD_NONE);
    input.seconds_of_day = NOON + 300;
    EXPECT(CLOTHES_CMD_EXTEND);
    CHECK(control.position == CLOTHES_POSITION_MOVING_EXTEND);
    CHECK(control.last_extend_date == 20260501);
    return 0;
}

static int daylight_extend_is_once_per_day(void)
{
    SETUP();
    control.position = CLOTHES_POSITION_RETRACTED;
    CLOCK(20260501, NOON);
    SET_LIGHT(5000.0f);
    EXPECT(CLOTHES_CMD_NONE);
    input.seconds_of_day = NOON + 300;
    EXPECT(CLOTHES_CMD_EXTEND);
    FINISH(CLOTHES_CMD_EXTEND);

    /* 同一天手动收回后不再自动推出，避免和用户操作打架。 */
    input.manual_command = CLOTHES_CMD_RETRACT;
    EXPECT(CLOTHES_CMD_RETRACT);
    FINISH(CLOTHES_CMD_RETRACT);
    input.manual_command = CLOTHES_CMD_NONE;
    input.seconds_of_day = NOON + 2000;
    EXPECT(CLOTHES_CMD_NONE);
    CHECK(control.position == CLOTHES_POSITION_RETRACTED);

    /* 次日重新累计并允许自动推出。 */
    CLOCK(20260502, NOON);
    EXPECT(CLOTHES_CMD_NONE);
    input.seconds_of_day = NOON + 300;
    EXPECT(CLOTHES_CMD_EXTEND);
    return 0;
}

static int dimming_resets_the_dwell_timer(void)
{
    SETUP();
    control.position = CLOTHES_POSITION_RETRACTED;
    CLOCK(20260501, NOON);
    SET_LIGHT(5000.0f);
    EXPECT(CLOTHES_CMD_NONE);
    input.seconds_of_day = NOON + 200;
    EXPECT(CLOTHES_CMD_NONE);

    input.lux = 100.0f;
    EXPECT(CLOTHES_CMD_NONE);
    CHECK(control.bright_since_seconds == 0);

    SET_LIGHT(5000.0f);
    EXPECT(CLOTHES_CMD_NONE);
    input.seconds_of_day = NOON + 499;
    EXPECT(CLOTHES_CMD_NONE);
    input.seconds_of_day = NOON + 500;
    EXPECT(CLOTHES_CMD_EXTEND);
    return 0;
}

static int rain_blocks_daylight_extend(void)
{
    SETUP();
    control.position = CLOTHES_POSITION_RETRACTED;
    CLOCK(20260501, NOON);
    input.rain_valid = true;
    input.raining = true;
    SET_LIGHT(5000.0f);
    EXPECT(CLOTHES_CMD_NONE);
    CHECK(control.bright_since_seconds == 0);
    input.seconds_of_day = NOON + 600;
    EXPECT(CLOTHES_CMD_NONE);

    input.raining = false;
    EXPECT(CLOTHES_CMD_NONE);
    CHECK(control.bright_since_seconds == NOON + 600);
    input.seconds_of_day = NOON + 900;
    EXPECT(CLOTHES_CMD_EXTEND);
    return 0;
}

static int daylight_extend_only_inside_window(void)
{
    SETUP();
    control.position = CLOTHES_POSITION_RETRACTED;
    SET_LIGHT(5000.0f);
    CLOCK(20260501, CLOTHES_AUTO_EXTEND_START_SECONDS - 1);
    EXPECT(CLOTHES_CMD_NONE);
    CHECK(control.bright_since_seconds == 0);
    CLOCK(20260501, 1000);                    /* 清晨 00:16，仍在窗口外 */
    EXPECT(CLOTHES_CMD_NONE);
    CHECK(control.bright_since_seconds == 0);

    CLOCK(20260501, CLOTHES_AUTO_EXTEND_START_SECONDS);
    EXPECT(CLOTHES_CMD_NONE);
    CHECK(control.bright_since_seconds == CLOTHES_AUTO_EXTEND_START_SECONDS);

    input.seconds_of_day = CLOTHES_AUTO_EXTEND_END_SECONDS;
    EXPECT(CLOTHES_CMD_NONE);
    CHECK(control.bright_since_seconds == 0);
    input.seconds_of_day += 300;
    EXPECT(CLOTHES_CMD_NONE);
    return 0;
}

static int daylight_extend_requires_retracted_position(void)
{
    SETUP();
    control.position = CLOTHES_POSITION_EXTENDED;
    CLOCK(20260501, NOON);
    SET_LIGHT(5000.0f);
    EXPECT(CLOTHES_CMD_NONE);
    input.seconds_of_day = NOON + 600;
    EXPECT(CLOTHES_CMD_NONE);
    CHECK(control.bright_since_seconds == 0);
    return 0;
}

static int stale_or_non_finite_light_never_extends(void)
{
    SETUP();
    control.position = CLOTHES_POSITION_RETRACTED;
    CLOCK(20260501, NOON);
    input.light_valid = false;
    input.lux = 100000.0f;
    EXPECT(CLOTHES_CMD_NONE);
    CHECK(control.bright_since_seconds == 0);

    input.light_valid = true;
    input.lux = NAN;
    EXPECT(CLOTHES_CMD_NONE);
    CHECK(control.bright_since_seconds == 0);

    input.lux = INFINITY;
    EXPECT(CLOTHES_CMD_NONE);
    CHECK(control.bright_since_seconds == 0);
    return 0;
}

static int failed_daylight_extend_is_consumed_for_the_day(void)
{
    SETUP();
    control.position = CLOTHES_POSITION_RETRACTED;
    CLOCK(20260501, NOON);
    SET_LIGHT(5000.0f);
    EXPECT(CLOTHES_CMD_NONE);
    input.seconds_of_day = NOON + 300;
    EXPECT(CLOTHES_CMD_EXTEND);
    clothes_control_failed(&control);
    CHECK(control.position == CLOTHES_POSITION_UNKNOWN);
    CHECK(control.last_extend_date == 20260501);
    input.seconds_of_day = NOON + 900;
    EXPECT(CLOTHES_CMD_NONE);
    return 0;
}

static int manual_mode_disables_scheduled_rules(void)
{
    SETUP();
    control.position = CLOTHES_POSITION_RETRACTED;
    input.mode = CLOTHES_MODE_MANUAL;
    CLOCK(20260501, EVENING);
    EXPECT(CLOTHES_CMD_NONE);
    CHECK(control.last_evening_date == 0);

    CLOCK(20260501, NOON);
    SET_LIGHT(9000.0f);
    EXPECT(CLOTHES_CMD_NONE);
    input.seconds_of_day = NOON + 600;
    EXPECT(CLOTHES_CMD_NONE);
    CHECK(control.bright_since_seconds == 0);
    CHECK(control.last_extend_date == 0);
    return 0;
}

static int manual_mode_keeps_rain_retraction(void)
{
    SETUP();
    control.position = CLOTHES_POSITION_EXTENDED;
    input.mode = CLOTHES_MODE_MANUAL;
    input.rain_valid = true;
    input.raining = true;
    EXPECT(CLOTHES_CMD_RETRACT);
    FINISH(CLOTHES_CMD_RETRACT);
    CHECK(control.position == CLOTHES_POSITION_RETRACTED);
    return 0;
}

static int manual_mode_still_allows_button_and_web_commands(void)
{
    SETUP();
    input.mode = CLOTHES_MODE_MANUAL;
    input.manual_command = CLOTHES_CMD_RETRACT;
    EXPECT(CLOTHES_CMD_RETRACT);
    FINISH(CLOTHES_CMD_RETRACT);
    input.manual_command = CLOTHES_CMD_EXTEND;
    EXPECT(CLOTHES_CMD_EXTEND);
    FINISH(CLOTHES_CMD_EXTEND);
    CHECK(control.position == CLOTHES_POSITION_EXTENDED);
    return 0;
}

struct test_case { const char *name; int (*run)(void); };
#define TEST(name) {#name, name}
static const struct test_case cases[] = {
    TEST(boot_position_is_unknown),
    TEST(six_pm_boundary_fires_once),
    TEST(boot_after_six_pm_catches_up),
    TEST(retracted_early_skips_evening),
    TEST(already_retracting_skips_evening),
    TEST(reextended_before_evening_is_retracted),
    TEST(evening_reverses_ongoing_extension),
    TEST(manual_extension_after_evening_stays_extended),
    TEST(skipped_evening_is_still_consumed),
    TEST(next_day_rearms_at_six_pm),
    TEST(year_rollover_rearms),
    TEST(clock_rollback_cannot_replay_evening),
    TEST(invalid_clock_does_not_consume_event),
    TEST(invalid_seconds_and_zero_date_are_ignored),
    TEST(invalid_clock_keeps_buttons_and_rain_working),
    TEST(first_rain_retracts_once),
    TEST(rain_early_skips_evening),
    TEST(rain_wins_over_manual_extend),
    TEST(evening_wins_over_manual_extend),
    TEST(missing_rain_reading_preserves_wet_lockout),
    TEST(rain_stopping_does_not_extend),
    TEST(later_rain_retracts_again),
    TEST(manual_retract_reverses_extension),
    TEST(dry_manual_extend_can_reverse_retraction),
    TEST(completion_requires_current_command),
    TEST(failed_evening_does_not_busy_retry),
    TEST(failed_rain_waits_for_new_event),
    TEST(next_day_can_retry_failed_evening),
    TEST(simultaneous_events_produce_one_command),
    TEST(null_arguments_are_safe),
    TEST(first_toggle_retracts_unknown_position),
    TEST(successive_toggles_alternate_completed_positions),
    TEST(toggle_reverses_in_both_moving_states),
    TEST(failed_movement_next_toggle_retries_retraction),
    TEST(automatic_rain_retraction_updates_next_toggle),
    TEST(wet_blocked_toggles_do_not_change_next_dry_action),
    TEST(rain_start_overrides_toggle_extension),
    TEST(evening_overrides_toggle_then_next_dry_press_extends),
    TEST(toggle_retracted_early_skips_evening),
    TEST(daylight_extend_requires_full_dwell),
    TEST(daylight_extend_is_once_per_day),
    TEST(dimming_resets_the_dwell_timer),
    TEST(rain_blocks_daylight_extend),
    TEST(daylight_extend_only_inside_window),
    TEST(daylight_extend_requires_retracted_position),
    TEST(stale_or_non_finite_light_never_extends),
    TEST(failed_daylight_extend_is_consumed_for_the_day),
    TEST(manual_mode_disables_scheduled_rules),
    TEST(manual_mode_keeps_rain_retraction),
    TEST(manual_mode_still_allows_button_and_web_commands),
};

int clothes_test_count(void) { return (int)(sizeof(cases) / sizeof(cases[0])); }
const char *clothes_test_name(int index) { return cases[index].name; }
int clothes_test_run(int index) { return cases[index].run(); }
