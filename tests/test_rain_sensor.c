/* Test production rain_sensor.c with mock GPIO levels; no board required. */
#include <stddef.h>
#include "rain_sensor.h"
#include "driver/gpio.h"

#define RAIN_GPIO 27
#define CHECK(condition) do { if (!(condition)) return __LINE__; } while (0)

static gpio_config_t last_config;
static esp_err_t config_result;
static int levels[GPIO_NUM_MAX];
static int gpio_reads[GPIO_NUM_MAX];
static int config_calls, read_calls, mock_fault;

static void reset_mock(void)
{
    last_config = (gpio_config_t){0};
    config_result = ESP_OK;
    for (int i = 0; i < GPIO_NUM_MAX; ++i) {
        levels[i] = 1;
        gpio_reads[i] = 0;
    }
    config_calls = read_calls = mock_fault = 0;
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
    ++gpio_reads[gpio];
    return levels[gpio];
}

static int init_active_low_enables_pullup(void)
{
    rain_sensor_t sensor = {0};
    CHECK(rain_sensor_init(&sensor, RAIN_GPIO, 0) == ESP_OK);
    CHECK(sensor.initialized);
    CHECK(sensor.gpio == RAIN_GPIO && sensor.active_level == 0);
    CHECK(config_calls == 1);
    CHECK(last_config.pin_bit_mask == (1ULL << RAIN_GPIO));
    CHECK(last_config.mode == GPIO_MODE_INPUT);
    CHECK(last_config.pull_up_en == GPIO_PULLUP_ENABLE);
    CHECK(last_config.pull_down_en == GPIO_PULLDOWN_DISABLE);
    CHECK(last_config.intr_type == GPIO_INTR_DISABLE);
    return 0;
}

static int init_active_high_enables_pulldown(void)
{
    rain_sensor_t sensor = {0};
    CHECK(rain_sensor_init(&sensor, 26, 1) == ESP_OK);
    CHECK(last_config.pull_up_en == GPIO_PULLUP_DISABLE);
    CHECK(last_config.pull_down_en == GPIO_PULLDOWN_ENABLE);
    CHECK(last_config.pin_bit_mask == (1ULL << 26));
    return 0;
}

static int init_skips_internal_pull_on_input_only_gpio(void)
{
    /* GPIO34~39 只能输入、没有内部上下拉，不能配置成上下拉输入。 */
    static const int pins[] = { 34, 35, 36, 37, 38, 39 };
    for (size_t i = 0; i < sizeof(pins) / sizeof(pins[0]); ++i) {
        rain_sensor_t sensor = {0};
        CHECK(rain_sensor_init(&sensor, (gpio_num_t)pins[i], 0) == ESP_OK);
        CHECK(last_config.pull_up_en == GPIO_PULLUP_DISABLE);
        CHECK(last_config.pull_down_en == GPIO_PULLDOWN_DISABLE);
    }
    return 0;
}

static int init_rejects_null_and_invalid_arguments(void)
{
    CHECK(rain_sensor_init(NULL, RAIN_GPIO, 0) == ESP_ERR_INVALID_ARG);

    static const int invalid_pins[] = { -1, 40, 100 };
    for (size_t i = 0; i < sizeof(invalid_pins) / sizeof(invalid_pins[0]); ++i) {
        rain_sensor_t sensor = {0};
        CHECK(rain_sensor_init(&sensor, (gpio_num_t)invalid_pins[i], 0) == ESP_ERR_INVALID_ARG);
        CHECK(!sensor.initialized);
    }
    static const int invalid_levels[] = { -1, 2, 5 };
    for (size_t i = 0; i < sizeof(invalid_levels) / sizeof(invalid_levels[0]); ++i) {
        rain_sensor_t sensor = {0};
        CHECK(rain_sensor_init(&sensor, RAIN_GPIO, invalid_levels[i]) == ESP_ERR_INVALID_ARG);
        CHECK(!sensor.initialized);
    }
    CHECK(config_calls == 0);
    return 0;
}

static int init_rejects_duplicate_without_reconfiguring(void)
{
    rain_sensor_t sensor = {0};
    CHECK(rain_sensor_init(&sensor, RAIN_GPIO, 0) == ESP_OK);
    CHECK(rain_sensor_init(&sensor, 26, 1) == ESP_ERR_INVALID_STATE);
    CHECK(sensor.gpio == RAIN_GPIO && sensor.active_level == 0);
    CHECK(config_calls == 1);
    return 0;
}

static int init_gpio_failure_keeps_uninitialized_and_retries(void)
{
    rain_sensor_t sensor = {0};
    config_result = ESP_ERR_INVALID_ARG;
    CHECK(rain_sensor_init(&sensor, RAIN_GPIO, 0) == ESP_ERR_INVALID_ARG);
    CHECK(!sensor.initialized);

    config_result = ESP_OK;
    CHECK(rain_sensor_init(&sensor, RAIN_GPIO, 0) == ESP_OK);
    CHECK(sensor.initialized);
    CHECK(config_calls == 2);
    return 0;
}

static int read_requires_initialized_sensor(void)
{
    rain_sensor_t sensor = {0};
    bool raining = true;
    CHECK(rain_sensor_read(NULL, &raining) == ESP_ERR_INVALID_ARG);
    CHECK(rain_sensor_read(&sensor, NULL) == ESP_ERR_INVALID_ARG);
    CHECK(rain_sensor_read(&sensor, &raining) == ESP_ERR_INVALID_STATE);
    CHECK(raining);
    CHECK(read_calls == 0);
    return 0;
}

static int read_maps_active_low(void)
{
    rain_sensor_t sensor = {0};
    CHECK(rain_sensor_init(&sensor, RAIN_GPIO, 0) == ESP_OK);

    levels[RAIN_GPIO] = 1;
    bool raining = true;
    CHECK(rain_sensor_read(&sensor, &raining) == ESP_OK);
    CHECK(!raining);

    levels[RAIN_GPIO] = 0;
    raining = false;
    CHECK(rain_sensor_read(&sensor, &raining) == ESP_OK);
    CHECK(raining);
    CHECK(gpio_reads[RAIN_GPIO] == 2 && read_calls == 2);
    return 0;
}

static int read_maps_active_high(void)
{
    rain_sensor_t sensor = {0};
    CHECK(rain_sensor_init(&sensor, 26, 1) == ESP_OK);

    levels[26] = 1;
    bool raining = false;
    CHECK(rain_sensor_read(&sensor, &raining) == ESP_OK);
    CHECK(raining);

    levels[26] = 0;
    raining = true;
    CHECK(rain_sensor_read(&sensor, &raining) == ESP_OK);
    CHECK(!raining);
    return 0;
}

static int two_objects_read_independent_pins(void)
{
    rain_sensor_t dry = {0};
    rain_sensor_t wet = {0};
    CHECK(rain_sensor_init(&dry, RAIN_GPIO, 0) == ESP_OK);
    CHECK(rain_sensor_init(&wet, 26, 1) == ESP_OK);

    levels[RAIN_GPIO] = 1;   /* 干：低电平有效，读到高电平 -> 无雨 */
    levels[26] = 1;          /* 湿：高电平有效，读到高电平 -> 有雨 */
    bool dry_raining = true;
    bool wet_raining = false;
    CHECK(rain_sensor_read(&dry, &dry_raining) == ESP_OK);
    CHECK(rain_sensor_read(&wet, &wet_raining) == ESP_OK);
    CHECK(!dry_raining);
    CHECK(wet_raining);
    CHECK(gpio_reads[RAIN_GPIO] == 1 && gpio_reads[26] == 1);
    return 0;
}

struct test_case { const char *name; int (*run)(void); };
#define TEST(name) {#name, name}
static const struct test_case cases[] = {
    TEST(init_active_low_enables_pullup),
    TEST(init_active_high_enables_pulldown),
    TEST(init_skips_internal_pull_on_input_only_gpio),
    TEST(init_rejects_null_and_invalid_arguments),
    TEST(init_rejects_duplicate_without_reconfiguring),
    TEST(init_gpio_failure_keeps_uninitialized_and_retries),
    TEST(read_requires_initialized_sensor),
    TEST(read_maps_active_low),
    TEST(read_maps_active_high),
    TEST(two_objects_read_independent_pins),
};

int rain_sensor_test_count(void) { return (int)(sizeof(cases) / sizeof(cases[0])); }
const char *rain_sensor_test_name(int index) { return cases[index].name; }
int rain_sensor_test_run(int index)
{
    reset_mock();
    int line = cases[index].run();
    return line ? line : mock_fault;
}
