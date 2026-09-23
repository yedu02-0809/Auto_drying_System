/* Exercise production policy with user actions, clock changes and motor results. */
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
};

int clothes_test_count(void) { return (int)(sizeof(cases) / sizeof(cases[0])); }
const char *clothes_test_name(int index) { return cases[index].name; }
int clothes_test_run(int index) { return cases[index].run(); }
