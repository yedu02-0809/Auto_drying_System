/* Test production servo.c with mock LEDC/GPIO so no hardware is required.
 * Duty values assume SG90 calibration: 1000us..2000us over 0..180 deg,
 * 14-bit resolution and a 20000us period. */
#include <math.h>
#include <stddef.h>
#include "servo.h"
#include "driver/ledc.h"

#define SERVO_GPIO 18
#define CHECK(condition) do { if (!(condition)) return __LINE__; } while (0)

static ledc_timer_config_t last_timer;
static ledc_channel_config_t last_channel;
static ledc_mode_t last_mode;
static ledc_channel_t last_channel_id;
static uint32_t last_duty;
static esp_err_t timer_result, channel_result, set_result, update_result;
static int timer_calls, channel_calls, set_duty_calls, update_calls, mock_fault;

static void reset_mock(void)
{
    last_timer = (ledc_timer_config_t){0};
    last_channel = (ledc_channel_config_t){0};
    last_mode = LEDC_HIGH_SPEED_MODE;
    last_channel_id = LEDC_CHANNEL_3;
    last_duty = 0;
    timer_result = channel_result = set_result = update_result = ESP_OK;
    timer_calls = channel_calls = set_duty_calls = update_calls = mock_fault = 0;
}

esp_err_t ledc_timer_config(const ledc_timer_config_t *timer_conf)
{
    ++timer_calls;
    if (timer_conf == NULL) {
        mock_fault = __LINE__;
        return ESP_ERR_INVALID_ARG;
    }
    last_timer = *timer_conf;
    return timer_result;
}

esp_err_t ledc_channel_config(const ledc_channel_config_t *ledc_conf)
{
    ++channel_calls;
    if (ledc_conf == NULL) {
        mock_fault = __LINE__;
        return ESP_ERR_INVALID_ARG;
    }
    last_channel = *ledc_conf;
    return channel_result;
}

esp_err_t ledc_set_duty(ledc_mode_t speed_mode, ledc_channel_t channel, uint32_t duty)
{
    ++set_duty_calls;
    last_mode = speed_mode;
    last_channel_id = channel;
    last_duty = duty;
    return set_result;
}

esp_err_t ledc_update_duty(ledc_mode_t speed_mode, ledc_channel_t channel)
{
    ++update_calls;
    last_mode = speed_mode;
    last_channel_id = channel;
    return update_result;
}

static int init_configures_50hz_14bit_timer(void)
{
    servo_t servo = {0};
    CHECK(servo_init(&servo, SERVO_GPIO) == ESP_OK);
    CHECK(servo.initialized);
    CHECK(servo.gpio == SERVO_GPIO);
    CHECK(timer_calls == 1 && channel_calls == 1);
    CHECK(last_timer.speed_mode == LEDC_LOW_SPEED_MODE);
    CHECK(last_timer.timer_num == LEDC_TIMER_0);
    CHECK(last_timer.duty_resolution == LEDC_TIMER_14_BIT);
    CHECK(last_timer.freq_hz == 50);
    CHECK(last_timer.clk_cfg == LEDC_AUTO_CLK);
    return 0;
}

static int init_configures_channel_with_center_duty(void)
{
    servo_t servo = {0};
    CHECK(servo_init(&servo, SERVO_GPIO) == ESP_OK);
    CHECK(last_channel.gpio_num == SERVO_GPIO);
    CHECK(last_channel.speed_mode == LEDC_LOW_SPEED_MODE);
    CHECK(last_channel.channel == LEDC_CHANNEL_0);
    CHECK(last_channel.timer_sel == LEDC_TIMER_0);
    CHECK(last_channel.intr_type == LEDC_INTR_DISABLE);
    CHECK(last_channel.hpoint == 0);
    /* 90 deg -> 1500us -> 1500*16384/20000 = 1228.8 -> 1229 */
    CHECK(last_channel.duty == 1229);
    return 0;
}

static int init_rejects_null_and_invalid_gpio(void)
{
    CHECK(servo_init(NULL, SERVO_GPIO) == ESP_ERR_INVALID_ARG);
    CHECK(timer_calls == 0 && channel_calls == 0);

    static const int invalid[] = { -1, 20, 24, 28, 30, 34, 39, 40, 100 };
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        servo_t servo = {0};
        CHECK(servo_init(&servo, (gpio_num_t)invalid[i]) == ESP_ERR_INVALID_ARG);
        CHECK(!servo.initialized);
    }
    CHECK(timer_calls == 0 && channel_calls == 0);
    return 0;
}

static int init_rejects_duplicate_without_reconfiguring(void)
{
    servo_t servo = {0};
    CHECK(servo_init(&servo, SERVO_GPIO) == ESP_OK);
    CHECK(servo_init(&servo, 19) == ESP_ERR_INVALID_STATE);
    CHECK(servo.gpio == SERVO_GPIO);
    CHECK(timer_calls == 1 && channel_calls == 1);
    return 0;
}

static int init_timer_failure_keeps_uninitialized_and_retries(void)
{
    servo_t servo = {0};
    timer_result = ESP_ERR_INVALID_ARG;
    CHECK(servo_init(&servo, SERVO_GPIO) == ESP_ERR_INVALID_ARG);
    CHECK(!servo.initialized);
    CHECK(channel_calls == 0);

    timer_result = ESP_OK;
    CHECK(servo_init(&servo, SERVO_GPIO) == ESP_OK);
    CHECK(servo.initialized);
    CHECK(timer_calls == 2 && channel_calls == 1);
    return 0;
}

static int init_channel_failure_keeps_uninitialized_and_retries(void)
{
    servo_t servo = {0};
    channel_result = ESP_ERR_INVALID_STATE;
    CHECK(servo_init(&servo, SERVO_GPIO) == ESP_ERR_INVALID_STATE);
    CHECK(!servo.initialized);
    CHECK(servo.gpio == 0);

    channel_result = ESP_OK;
    CHECK(servo_init(&servo, SERVO_GPIO) == ESP_OK);
    CHECK(servo.initialized);
    CHECK(timer_calls == 2 && channel_calls == 2);
    return 0;
}

static int set_angle_requires_initialized_object(void)
{
    servo_t servo = {0};
    CHECK(servo_set_angle(NULL, 90.0f) == ESP_ERR_INVALID_ARG);
    CHECK(servo_set_angle(&servo, 90.0f) == ESP_ERR_INVALID_STATE);
    CHECK(set_duty_calls == 0 && update_calls == 0);
    return 0;
}

static int set_angle_rejects_out_of_range_and_non_finite(void)
{
    servo_t servo = {0};
    CHECK(servo_init(&servo, SERVO_GPIO) == ESP_OK);
    const float invalid[] = { -0.1f, 180.1f, 1000.0f, NAN, INFINITY, -INFINITY };
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        CHECK(servo_set_angle(&servo, invalid[i]) == ESP_ERR_INVALID_ARG);
    }
    CHECK(set_duty_calls == 0 && update_calls == 0);
    return 0;
}

static int set_angle_converts_angle_to_duty(void)
{
    servo_t servo = {0};
    CHECK(servo_init(&servo, SERVO_GPIO) == ESP_OK);

    static const struct { float angle; uint32_t duty; } cases[] = {
        { 0.0f, 819 },    /* 1000us */
        { 45.0f, 1024 },  /* 1250us 收回 */
        { 90.0f, 1229 },  /* 1500us 中位 */
        { 135.0f, 1434 }, /* 1750us 伸出 */
        { 180.0f, 1638 }, /* 2000us */
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        CHECK(servo_set_angle(&servo, cases[i].angle) == ESP_OK);
        CHECK(last_duty == cases[i].duty);
        CHECK(set_duty_calls == (int)i + 1 && update_calls == (int)i + 1);
    }
    return 0;
}

static int set_angle_uses_low_speed_channel_zero(void)
{
    servo_t servo = {0};
    CHECK(servo_init(&servo, SERVO_GPIO) == ESP_OK);
    CHECK(servo_set_angle(&servo, 135.0f) == ESP_OK);
    CHECK(last_mode == LEDC_LOW_SPEED_MODE);
    CHECK(last_channel_id == LEDC_CHANNEL_0);
    return 0;
}

static int set_duty_failure_skips_update(void)
{
    servo_t servo = {0};
    CHECK(servo_init(&servo, SERVO_GPIO) == ESP_OK);
    set_result = ESP_ERR_TIMEOUT;
    CHECK(servo_set_angle(&servo, 45.0f) == ESP_ERR_TIMEOUT);
    CHECK(set_duty_calls == 1 && update_calls == 0);
    return 0;
}

static int update_duty_failure_propagates(void)
{
    servo_t servo = {0};
    CHECK(servo_init(&servo, SERVO_GPIO) == ESP_OK);
    update_result = ESP_FAIL;
    CHECK(servo_set_angle(&servo, 45.0f) == ESP_FAIL);
    CHECK(set_duty_calls == 1 && update_calls == 1);
    /* 失败后对象仍可用，下一次成功调用不受影响。 */
    update_result = ESP_OK;
    CHECK(servo_set_angle(&servo, 45.0f) == ESP_OK);
    CHECK(update_calls == 2);
    return 0;
}

struct test_case { const char *name; int (*run)(void); };
#define TEST(name) {#name, name}
static const struct test_case cases[] = {
    TEST(init_configures_50hz_14bit_timer),
    TEST(init_configures_channel_with_center_duty),
    TEST(init_rejects_null_and_invalid_gpio),
    TEST(init_rejects_duplicate_without_reconfiguring),
    TEST(init_timer_failure_keeps_uninitialized_and_retries),
    TEST(init_channel_failure_keeps_uninitialized_and_retries),
    TEST(set_angle_requires_initialized_object),
    TEST(set_angle_rejects_out_of_range_and_non_finite),
    TEST(set_angle_converts_angle_to_duty),
    TEST(set_angle_uses_low_speed_channel_zero),
    TEST(set_duty_failure_skips_update),
    TEST(update_duty_failure_propagates),
};

int servo_test_count(void) { return (int)(sizeof(cases) / sizeof(cases[0])); }
const char *servo_test_name(int index) { return cases[index].name; }
int servo_test_run(int index)
{
    reset_mock();
    int line = cases[index].run();
    return line ? line : mock_fault;
}
