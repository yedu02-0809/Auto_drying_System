/* Test production key.c using real microsecond deadlines and mock GPIO levels. */
#include <stddef.h>
#include "key.h"
#include "esp_timer.h"

#define EXTEND_GPIO 25
#define RETRACT_GPIO 26
#define CHECK(condition) do { if (!(condition)) return __LINE__; } while (0)
#define EXPECT_POLL(keys, time, expected) do { \
    key_event_t actual_event = (key_event_t)99; \
    now_us = (time); \
    CHECK(key_poll((keys), &actual_event) == ESP_OK); \
    CHECK(actual_event == (expected)); \
    CHECK(mock_fault == 0); \
} while (0)

static int levels[GPIO_NUM_MAX];
static int64_t now_us;
static gpio_config_t last_config;
static esp_err_t config_result;
static int config_calls, read_calls, clock_calls, mock_fault;

static void reset_mock(void)
{
    for (int i = 0; i < GPIO_NUM_MAX; ++i) levels[i] = 1;
    now_us = 0;
    config_result = ESP_OK;
    config_calls = read_calls = clock_calls = mock_fault = 0;
}

esp_err_t gpio_config(const gpio_config_t *config)
{
    ++config_calls;
    if (config == NULL) {
        mock_fault = __LINE__;
        return ESP_ERR_INVALID_ARG;
    }
    last_config = *config;
    return config_result;
}

int gpio_get_level(gpio_num_t gpio)
{
    ++read_calls;
    if (gpio < 0 || gpio >= GPIO_NUM_MAX) {
        mock_fault = __LINE__;
        return 1;
    }
    return levels[gpio];
}

int64_t esp_timer_get_time(void)
{
    ++clock_calls;
    return now_us;
}

static int init_configures_two_pullup_inputs(void)
{
    key_pair_t keys = {.initialized = false};
    CHECK(key_init(&keys, EXTEND_GPIO, RETRACT_GPIO) == ESP_OK);
    CHECK(keys.initialized);
    CHECK(config_calls == 1 && read_calls == 2);
    CHECK(last_config.pin_bit_mask == ((1ULL << EXTEND_GPIO) | (1ULL << RETRACT_GPIO)));
    CHECK(last_config.mode == GPIO_MODE_INPUT);
    CHECK(last_config.pull_up_en == GPIO_PULLUP_ENABLE);
    CHECK(last_config.pull_down_en == GPIO_PULLDOWN_DISABLE);
    CHECK(last_config.intr_type == GPIO_INTR_DISABLE);
    EXPECT_POLL(&keys, 0, KEY_EVENT_NONE);
    EXPECT_POLL(&keys, 5000000, KEY_EVENT_NONE);
    return 0;
}

static int init_rejects_invalid_arguments(void)
{
    key_pair_t keys = {.initialized = false};
    const gpio_num_t invalid[] = {-1, GPIO_NUM_MAX, 64, 20, 24, 28, 31, 34, 39};
    CHECK(key_init(NULL, EXTEND_GPIO, RETRACT_GPIO) == ESP_ERR_INVALID_ARG);
    CHECK(key_init(&keys, EXTEND_GPIO, EXTEND_GPIO) == ESP_ERR_INVALID_ARG);
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        CHECK(key_init(&keys, invalid[i], RETRACT_GPIO) == ESP_ERR_INVALID_ARG);
        CHECK(key_init(&keys, EXTEND_GPIO, invalid[i]) == ESP_ERR_INVALID_ARG);
    }
    CHECK(!keys.initialized && config_calls == 0 && read_calls == 0 && clock_calls == 0);
    return 0;
}

static int init_rejects_duplicate_without_reconfiguring(void)
{
    key_pair_t keys = {.initialized = false};
    CHECK(key_init(&keys, EXTEND_GPIO, RETRACT_GPIO) == ESP_OK);
    CHECK(key_init(&keys, 32, 33) == ESP_ERR_INVALID_STATE);
    CHECK(config_calls == 1 && read_calls == 2);
    levels[EXTEND_GPIO] = 0;
    EXPECT_POLL(&keys, 100, KEY_EVENT_NONE);
    EXPECT_POLL(&keys, 30100, KEY_EVENT_EXTEND);
    return 0;
}

static int init_gpio_failure_can_retry(void)
{
    key_pair_t keys = {.initialized = false};
    config_result = ESP_FAIL;
    CHECK(key_init(&keys, EXTEND_GPIO, RETRACT_GPIO) == ESP_FAIL);
    CHECK(!keys.initialized && read_calls == 0 && clock_calls == 0);
    config_result = ESP_OK;
    CHECK(key_init(&keys, EXTEND_GPIO, RETRACT_GPIO) == ESP_OK);
    CHECK(config_calls == 2 && read_calls == 2);
    levels[RETRACT_GPIO] = 0;
    EXPECT_POLL(&keys, 100, KEY_EVENT_NONE);
    EXPECT_POLL(&keys, 30100, KEY_EVENT_RETRACT);
    return 0;
}

static int poll_errors_preserve_output(void)
{
    key_pair_t keys = {.initialized = false};
    key_event_t event = KEY_EVENT_RETRACT;
    CHECK(key_poll(NULL, &event) == ESP_ERR_INVALID_ARG);
    CHECK(event == KEY_EVENT_RETRACT);
    CHECK(key_poll(&keys, NULL) == ESP_ERR_INVALID_ARG);
    CHECK(key_poll(&keys, &event) == ESP_ERR_INVALID_STATE);
    CHECK(event == KEY_EVENT_RETRACT);
    CHECK(read_calls == 0 && clock_calls == 0);
    CHECK(key_init(&keys, EXTEND_GPIO, RETRACT_GPIO) == ESP_OK);
    CHECK(key_poll(&keys, NULL) == ESP_ERR_INVALID_ARG);
    CHECK(read_calls == 2);
    return 0;
}

static int extend_requires_full_30ms(void)
{
    key_pair_t keys = {.initialized = false};
    CHECK(key_init(&keys, EXTEND_GPIO, RETRACT_GPIO) == ESP_OK);
    levels[EXTEND_GPIO] = 0;
    EXPECT_POLL(&keys, 10000, KEY_EVENT_NONE);
    EXPECT_POLL(&keys, 39999, KEY_EVENT_NONE);
    EXPECT_POLL(&keys, 40000, KEY_EVENT_EXTEND);
    return 0;
}

static int retract_requires_full_30ms(void)
{
    key_pair_t keys = {.initialized = false};
    CHECK(key_init(&keys, EXTEND_GPIO, RETRACT_GPIO) == ESP_OK);
    levels[RETRACT_GPIO] = 0;
    EXPECT_POLL(&keys, 10000, KEY_EVENT_NONE);
    EXPECT_POLL(&keys, 39999, KEY_EVENT_NONE);
    EXPECT_POLL(&keys, 40000, KEY_EVENT_RETRACT);
    return 0;
}

static int frequent_polls_cannot_replace_elapsed_time(void)
{
    key_pair_t keys = {.initialized = false};
    CHECK(key_init(&keys, EXTEND_GPIO, RETRACT_GPIO) == ESP_OK);
    levels[EXTEND_GPIO] = 0;
    EXPECT_POLL(&keys, 100, KEY_EVENT_NONE);
    for (int i = 0; i < 1000; ++i) EXPECT_POLL(&keys, 200, KEY_EVENT_NONE);
    EXPECT_POLL(&keys, 30099, KEY_EVENT_NONE);
    EXPECT_POLL(&keys, 30100, KEY_EVENT_EXTEND);
    return 0;
}

static int press_bounce_restarts_debounce(void)
{
    key_pair_t keys = {.initialized = false};
    CHECK(key_init(&keys, EXTEND_GPIO, RETRACT_GPIO) == ESP_OK);
    levels[EXTEND_GPIO] = 0;
    EXPECT_POLL(&keys, 1000, KEY_EVENT_NONE);
    levels[EXTEND_GPIO] = 1;
    EXPECT_POLL(&keys, 30999, KEY_EVENT_NONE);
    levels[EXTEND_GPIO] = 0;
    EXPECT_POLL(&keys, 31000, KEY_EVENT_NONE);
    EXPECT_POLL(&keys, 60999, KEY_EVENT_NONE);
    EXPECT_POLL(&keys, 61000, KEY_EVENT_EXTEND);
    return 0;
}

static int short_glitch_produces_no_event(void)
{
    key_pair_t keys = {.initialized = false};
    CHECK(key_init(&keys, EXTEND_GPIO, RETRACT_GPIO) == ESP_OK);
    levels[RETRACT_GPIO] = 0;
    EXPECT_POLL(&keys, 1000, KEY_EVENT_NONE);
    levels[RETRACT_GPIO] = 1;
    EXPECT_POLL(&keys, 29999, KEY_EVENT_NONE);
    EXPECT_POLL(&keys, 1000000, KEY_EVENT_NONE);
    return 0;
}

static int held_button_never_repeats(void)
{
    key_pair_t keys = {.initialized = false};
    CHECK(key_init(&keys, EXTEND_GPIO, RETRACT_GPIO) == ESP_OK);
    levels[EXTEND_GPIO] = 0;
    EXPECT_POLL(&keys, 0, KEY_EVENT_NONE);
    EXPECT_POLL(&keys, 30000, KEY_EVENT_EXTEND);
    EXPECT_POLL(&keys, 60000, KEY_EVENT_NONE);
    EXPECT_POLL(&keys, 60000000, KEY_EVENT_NONE);
    return 0;
}

static int release_bounce_does_not_rearm(void)
{
    key_pair_t keys = {.initialized = false};
    CHECK(key_init(&keys, EXTEND_GPIO, RETRACT_GPIO) == ESP_OK);
    levels[EXTEND_GPIO] = 0;
    EXPECT_POLL(&keys, 0, KEY_EVENT_NONE);
    EXPECT_POLL(&keys, 30000, KEY_EVENT_EXTEND);
    levels[EXTEND_GPIO] = 1;
    EXPECT_POLL(&keys, 40000, KEY_EVENT_NONE);
    EXPECT_POLL(&keys, 69999, KEY_EVENT_NONE);
    levels[EXTEND_GPIO] = 0;
    EXPECT_POLL(&keys, 70000, KEY_EVENT_NONE);
    EXPECT_POLL(&keys, 100000, KEY_EVENT_NONE);
    levels[EXTEND_GPIO] = 1;
    EXPECT_POLL(&keys, 110000, KEY_EVENT_NONE);
    EXPECT_POLL(&keys, 140000, KEY_EVENT_NONE);
    levels[EXTEND_GPIO] = 0;
    EXPECT_POLL(&keys, 150000, KEY_EVENT_NONE);
    EXPECT_POLL(&keys, 180000, KEY_EVENT_EXTEND);
    return 0;
}

static int retract_release_then_press_again(void)
{
    key_pair_t keys = {.initialized = false};
    CHECK(key_init(&keys, EXTEND_GPIO, RETRACT_GPIO) == ESP_OK);
    levels[RETRACT_GPIO] = 0;
    EXPECT_POLL(&keys, 0, KEY_EVENT_NONE);
    EXPECT_POLL(&keys, 30000, KEY_EVENT_RETRACT);
    levels[RETRACT_GPIO] = 1;
    EXPECT_POLL(&keys, 40000, KEY_EVENT_NONE);
    EXPECT_POLL(&keys, 70000, KEY_EVENT_NONE);
    levels[RETRACT_GPIO] = 0;
    EXPECT_POLL(&keys, 80000, KEY_EVENT_NONE);
    EXPECT_POLL(&keys, 110000, KEY_EVENT_RETRACT);
    return 0;
}

static int simultaneous_presses_prefer_retract(void)
{
    key_pair_t keys = {.initialized = false};
    CHECK(key_init(&keys, EXTEND_GPIO, RETRACT_GPIO) == ESP_OK);
    levels[EXTEND_GPIO] = levels[RETRACT_GPIO] = 0;
    EXPECT_POLL(&keys, 1000, KEY_EVENT_NONE);
    EXPECT_POLL(&keys, 31000, KEY_EVENT_RETRACT);
    EXPECT_POLL(&keys, 100000, KEY_EVENT_NONE);
    levels[RETRACT_GPIO] = 1;
    EXPECT_POLL(&keys, 110000, KEY_EVENT_NONE);
    EXPECT_POLL(&keys, 140000, KEY_EVENT_NONE);
    EXPECT_POLL(&keys, 1000000, KEY_EVENT_NONE);
    return 0;
}

static int held_retract_consumes_extend_without_deferred_action(void)
{
    key_pair_t keys = {.initialized = false};
    CHECK(key_init(&keys, EXTEND_GPIO, RETRACT_GPIO) == ESP_OK);
    levels[RETRACT_GPIO] = 0;
    EXPECT_POLL(&keys, 0, KEY_EVENT_NONE);
    EXPECT_POLL(&keys, 30000, KEY_EVENT_RETRACT);
    levels[EXTEND_GPIO] = 0;
    EXPECT_POLL(&keys, 40000, KEY_EVENT_NONE);
    EXPECT_POLL(&keys, 70000, KEY_EVENT_NONE);
    levels[RETRACT_GPIO] = 1;
    EXPECT_POLL(&keys, 80000, KEY_EVENT_NONE);
    EXPECT_POLL(&keys, 110000, KEY_EVENT_NONE);
    EXPECT_POLL(&keys, 150000, KEY_EVENT_NONE);
    levels[EXTEND_GPIO] = 1;
    EXPECT_POLL(&keys, 160000, KEY_EVENT_NONE);
    EXPECT_POLL(&keys, 190000, KEY_EVENT_NONE);
    levels[EXTEND_GPIO] = 0;
    EXPECT_POLL(&keys, 200000, KEY_EVENT_NONE);
    EXPECT_POLL(&keys, 230000, KEY_EVENT_EXTEND);
    return 0;
}

static int retract_can_override_held_extend(void)
{
    key_pair_t keys = {.initialized = false};
    CHECK(key_init(&keys, EXTEND_GPIO, RETRACT_GPIO) == ESP_OK);
    levels[EXTEND_GPIO] = 0;
    EXPECT_POLL(&keys, 0, KEY_EVENT_NONE);
    EXPECT_POLL(&keys, 30000, KEY_EVENT_EXTEND);
    levels[RETRACT_GPIO] = 0;
    EXPECT_POLL(&keys, 40000, KEY_EVENT_NONE);
    EXPECT_POLL(&keys, 70000, KEY_EVENT_RETRACT);
    return 0;
}

static int boot_held_extend_requires_stable_release(void)
{
    key_pair_t keys = {.initialized = false};
    levels[EXTEND_GPIO] = 0;
    CHECK(key_init(&keys, EXTEND_GPIO, RETRACT_GPIO) == ESP_OK);
    EXPECT_POLL(&keys, 0, KEY_EVENT_NONE);
    EXPECT_POLL(&keys, 100000, KEY_EVENT_NONE);
    levels[EXTEND_GPIO] = 1;
    EXPECT_POLL(&keys, 110000, KEY_EVENT_NONE);
    EXPECT_POLL(&keys, 139999, KEY_EVENT_NONE);
    levels[EXTEND_GPIO] = 0;
    EXPECT_POLL(&keys, 140000, KEY_EVENT_NONE);
    EXPECT_POLL(&keys, 170000, KEY_EVENT_NONE);
    levels[EXTEND_GPIO] = 1;
    EXPECT_POLL(&keys, 180000, KEY_EVENT_NONE);
    EXPECT_POLL(&keys, 210000, KEY_EVENT_NONE);
    levels[EXTEND_GPIO] = 0;
    EXPECT_POLL(&keys, 220000, KEY_EVENT_NONE);
    EXPECT_POLL(&keys, 250000, KEY_EVENT_EXTEND);
    return 0;
}

static int boot_held_retract_suppresses_extend(void)
{
    key_pair_t keys = {.initialized = false};
    levels[RETRACT_GPIO] = 0;
    CHECK(key_init(&keys, EXTEND_GPIO, RETRACT_GPIO) == ESP_OK);
    EXPECT_POLL(&keys, 100000, KEY_EVENT_NONE);
    levels[EXTEND_GPIO] = 0;
    EXPECT_POLL(&keys, 110000, KEY_EVENT_NONE);
    EXPECT_POLL(&keys, 140000, KEY_EVENT_NONE);
    levels[RETRACT_GPIO] = 1;
    EXPECT_POLL(&keys, 150000, KEY_EVENT_NONE);
    EXPECT_POLL(&keys, 180000, KEY_EVENT_NONE);
    levels[RETRACT_GPIO] = 0;
    EXPECT_POLL(&keys, 190000, KEY_EVENT_NONE);
    EXPECT_POLL(&keys, 220000, KEY_EVENT_RETRACT);
    return 0;
}

static int both_buttons_held_at_boot_do_not_trigger(void)
{
    key_pair_t keys = {.initialized = false};
    levels[EXTEND_GPIO] = levels[RETRACT_GPIO] = 0;
    CHECK(key_init(&keys, EXTEND_GPIO, RETRACT_GPIO) == ESP_OK);
    EXPECT_POLL(&keys, 100000, KEY_EVENT_NONE);
    levels[EXTEND_GPIO] = levels[RETRACT_GPIO] = 1;
    EXPECT_POLL(&keys, 110000, KEY_EVENT_NONE);
    EXPECT_POLL(&keys, 140000, KEY_EVENT_NONE);
    levels[EXTEND_GPIO] = levels[RETRACT_GPIO] = 0;
    EXPECT_POLL(&keys, 150000, KEY_EVENT_NONE);
    EXPECT_POLL(&keys, 180000, KEY_EVENT_RETRACT);
    return 0;
}

static int two_objects_have_independent_debounce_state(void)
{
    key_pair_t first = {.initialized = false}, second = {.initialized = false};
    CHECK(key_init(&first, EXTEND_GPIO, RETRACT_GPIO) == ESP_OK);
    CHECK(key_init(&second, 32, 33) == ESP_OK);
    levels[EXTEND_GPIO] = 0;
    EXPECT_POLL(&first, 1000, KEY_EVENT_NONE);
    levels[33] = 0;
    EXPECT_POLL(&second, 20000, KEY_EVENT_NONE);
    EXPECT_POLL(&first, 31000, KEY_EVENT_EXTEND);
    EXPECT_POLL(&second, 31000, KEY_EVENT_NONE);
    EXPECT_POLL(&second, 50000, KEY_EVENT_RETRACT);
    EXPECT_POLL(&first, 50000, KEY_EVENT_NONE);
    return 0;
}

static int debounce_uses_64bit_microsecond_clock(void)
{
    key_pair_t keys = {.initialized = false};
    const int64_t start = INT64_C(4294967280);
    now_us = start;
    CHECK(key_init(&keys, EXTEND_GPIO, RETRACT_GPIO) == ESP_OK);
    levels[EXTEND_GPIO] = 0;
    EXPECT_POLL(&keys, start, KEY_EVENT_NONE);
    EXPECT_POLL(&keys, start + 29999, KEY_EVENT_NONE);
    EXPECT_POLL(&keys, start + 30000, KEY_EVENT_EXTEND);
    EXPECT_POLL(&keys, start + INT64_C(4294967296), KEY_EVENT_NONE);
    return 0;
}

struct test_case { const char *name; int (*run)(void); };
#define TEST(name) {#name, name}
static const struct test_case cases[] = {
    TEST(init_configures_two_pullup_inputs),
    TEST(init_rejects_invalid_arguments),
    TEST(init_rejects_duplicate_without_reconfiguring),
    TEST(init_gpio_failure_can_retry),
    TEST(poll_errors_preserve_output),
    TEST(extend_requires_full_30ms),
    TEST(retract_requires_full_30ms),
    TEST(frequent_polls_cannot_replace_elapsed_time),
    TEST(press_bounce_restarts_debounce),
    TEST(short_glitch_produces_no_event),
    TEST(held_button_never_repeats),
    TEST(release_bounce_does_not_rearm),
    TEST(retract_release_then_press_again),
    TEST(simultaneous_presses_prefer_retract),
    TEST(held_retract_consumes_extend_without_deferred_action),
    TEST(retract_can_override_held_extend),
    TEST(boot_held_extend_requires_stable_release),
    TEST(boot_held_retract_suppresses_extend),
    TEST(both_buttons_held_at_boot_do_not_trigger),
    TEST(two_objects_have_independent_debounce_state),
    TEST(debounce_uses_64bit_microsecond_clock),
};

int key_test_count(void) { return (int)(sizeof(cases) / sizeof(cases[0])); }
const char *key_test_name(int index) { return cases[index].name; }
int key_test_run(int index)
{
    reset_mock();
    int line = cases[index].run();
    return line ? line : mock_fault;
}
