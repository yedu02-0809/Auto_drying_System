/* Test the production BH1750 driver with command-level I2C and RTOS mocks. */
#include "bh1750.h"
#include "freertos/task.h"

#define CHECK(condition) do { if (!(condition)) return __LINE__; } while (0)
#define MOCK_CHECK(condition) do { if (!(condition) && !mock_fault) mock_fault = __LINE__; } while (0)
#define COUNT(array) (sizeof(array) / sizeof((array)[0]))

struct mock_i2c_bus { int unused; };
struct mock_i2c_device { int unused; };
static struct mock_i2c_bus bus_storage;
static struct mock_i2c_device device_storage;
#define BUS (&bus_storage)
#define DEVICE (&device_storage)

enum operation { ADD, TRANSMIT, DELAY, RECEIVE, REMOVE };
static struct { enum operation kind; unsigned value; } events[64];
static unsigned event_count, transmit_count, fail_transmit;
static int mock_fault;
static esp_err_t add_result, transmit_result, receive_result, remove_result;
static uint8_t response[2];
static TickType_t wait_ticks;

static void reset_mock(void)
{
    event_count = transmit_count = fail_transmit = 0;
    mock_fault = 0;
    add_result = receive_result = remove_result = ESP_OK;
    transmit_result = ESP_ERR_TIMEOUT;
    wait_ticks = 0;
    response[0] = 0x01;
    response[1] = 0x20;
}

static void record(enum operation kind, unsigned value)
{
    MOCK_CHECK(event_count < COUNT(events));
    if (event_count < COUNT(events)) {
        events[event_count].kind = kind;
        events[event_count++].value = value;
    }
}

esp_err_t i2c_master_bus_add_device(i2c_master_bus_handle_t bus,
                                  const i2c_device_config_t *config,
                                  i2c_master_dev_handle_t *device)
{
    MOCK_CHECK(bus == BUS && config != NULL && device != NULL);
    if (config == NULL || device == NULL) return ESP_FAIL;
    MOCK_CHECK(config->dev_addr_length == I2C_ADDR_BIT_LEN_7);
    MOCK_CHECK(config->scl_speed_hz == 100000);
    record(ADD, config->device_address);
    if (add_result == ESP_OK) *device = DEVICE;
    return add_result;
}

esp_err_t i2c_master_bus_rm_device(i2c_master_dev_handle_t device)
{
    MOCK_CHECK(device == DEVICE);
    record(REMOVE, 0);
    return remove_result;
}

esp_err_t i2c_master_transmit(i2c_master_dev_handle_t device,
                           const uint8_t *data, size_t length, int timeout_ms)
{
    MOCK_CHECK(device == DEVICE && data != NULL && length == 1 && timeout_ms == 100);
    record(TRANSMIT, data != NULL && length == 1 ? data[0] : 0xFF);
    ++transmit_count;
    return transmit_count == fail_transmit ? transmit_result : ESP_OK;
}

esp_err_t i2c_master_receive(i2c_master_dev_handle_t device,
                          uint8_t *data, size_t length, int timeout_ms)
{
    MOCK_CHECK(device == DEVICE && data != NULL && length == 2 && timeout_ms == 100);
    /* Even at a tick boundary, the conversion must have at least 180ms. */
    MOCK_CHECK(wait_ticks > 0 && (wait_ticks - 1) * 1000U >= 180U * CONFIG_FREERTOS_HZ);
    record(RECEIVE, (unsigned)length);
    if (data != NULL && length == 2) {
        data[0] = response[0];
        data[1] = response[1];
    }
    return receive_result;
}

void vTaskDelay(TickType_t ticks)
{
    MOCK_CHECK(ticks > 0 && (ticks - 1) * 1000U >= 180U * CONFIG_FREERTOS_HZ);
    wait_ticks = ticks;
    record(DELAY, ticks);
}

static int check_init_sequence(uint8_t address)
{
    bh1750_t sensor = {0};
    const uint8_t commands[] = {0x01, 0x07, 0x42, 0x65, 0x00};
    CHECK(bh1750_init(&sensor, BUS, address) == ESP_OK);
    CHECK(sensor.i2c_dev == DEVICE && sensor.initialized);
    CHECK(event_count == 6 && events[0].kind == ADD && events[0].value == address);
    for (size_t i = 0; i < COUNT(commands); ++i) {
        CHECK(events[i + 1].kind == TRANSMIT && events[i + 1].value == commands[i]);
    }
    return 0;
}
static int init_default_address_sequence(void) { return check_init_sequence(0x23); }
static int init_alternate_address_sequence(void) { return check_init_sequence(0x5C); }

static int init_rejects_invalid_arguments(void)
{
    bh1750_t sensor = {0};
    const uint8_t invalid[] = {0, 0x22, 0x24, 0x46, 0x5D, 0xB8, 0xFF};
    CHECK(bh1750_init(NULL, BUS, 0x23) == ESP_ERR_INVALID_ARG);
    CHECK(bh1750_init(&sensor, NULL, 0x23) == ESP_ERR_INVALID_ARG);
    for (size_t i = 0; i < COUNT(invalid); ++i) {
        CHECK(bh1750_init(&sensor, BUS, invalid[i]) == ESP_ERR_INVALID_ARG);
    }
    CHECK(event_count == 0 && sensor.i2c_dev == NULL && !sensor.initialized);
    return 0;
}

static int init_rejects_duplicate_without_bus_access(void)
{
    bh1750_t sensor = {0};
    CHECK(bh1750_init(&sensor, BUS, 0x23) == ESP_OK);
    unsigned before = event_count;
    CHECK(bh1750_init(&sensor, BUS, 0x5C) == ESP_ERR_INVALID_STATE);
    CHECK(event_count == before && sensor.i2c_dev == DEVICE && sensor.initialized);
    return 0;
}

static int init_add_failure_is_retryable(void)
{
    bh1750_t sensor = {0};
    add_result = ESP_ERR_NO_MEM;
    CHECK(bh1750_init(&sensor, BUS, 0x23) == ESP_ERR_NO_MEM);
    CHECK(event_count == 1 && !sensor.initialized && sensor.i2c_dev == NULL);
    add_result = ESP_OK;
    CHECK(bh1750_init(&sensor, BUS, 0x23) == ESP_OK);
    CHECK(sensor.initialized);
    return 0;
}

static int every_init_command_failure_cleans_up(void)
{
    for (unsigned step = 1; step <= 5; ++step) {
        reset_mock();
        bh1750_t sensor = {0};
        fail_transmit = step;
        CHECK(bh1750_init(&sensor, BUS, 0x23) == ESP_ERR_TIMEOUT);
        CHECK(!sensor.initialized && sensor.i2c_dev == NULL);
        CHECK(transmit_count == step && event_count == step + 2);
        CHECK(events[event_count - 1].kind == REMOVE);
        CHECK(mock_fault == 0);
    }
    return 0;
}

static int init_cleanup_failure_preserves_handle_for_retry(void)
{
    bh1750_t sensor = {0};
    fail_transmit = 3;
    remove_result = ESP_FAIL;
    CHECK(bh1750_init(&sensor, BUS, 0x23) == ESP_ERR_TIMEOUT);
    CHECK(sensor.i2c_dev == DEVICE && !sensor.initialized);
    unsigned before = event_count;
    float lux = 123.0f;
    CHECK(bh1750_read_lux(&sensor, &lux) == ESP_ERR_INVALID_STATE && lux == 123.0f);
    CHECK(bh1750_init(&sensor, BUS, 0x23) == ESP_ERR_INVALID_STATE);
    CHECK(event_count == before);
    remove_result = ESP_OK;
    CHECK(bh1750_deinit(&sensor) == ESP_OK);
    CHECK(sensor.i2c_dev == NULL && !sensor.initialized);
    fail_transmit = 0;
    CHECK(bh1750_init(&sensor, BUS, 0x23) == ESP_OK && sensor.initialized);
    return 0;
}

static int init_command_failure_can_retry_after_cleanup(void)
{
    bh1750_t sensor = {0};
    fail_transmit = 5;
    CHECK(bh1750_init(&sensor, BUS, 0x23) == ESP_ERR_TIMEOUT);
    fail_transmit = 0;
    CHECK(bh1750_init(&sensor, BUS, 0x23) == ESP_OK);
    CHECK(sensor.initialized);
    return 0;
}

static int read_rejects_null_arguments_preserving_output(void)
{
    bh1750_t sensor = {.i2c_dev = DEVICE, .initialized = true};
    float lux = -12.5f;
    CHECK(bh1750_read_lux(NULL, &lux) == ESP_ERR_INVALID_ARG);
    CHECK(lux == -12.5f);
    CHECK(bh1750_read_lux(&sensor, NULL) == ESP_ERR_INVALID_ARG);
    CHECK(event_count == 0);
    return 0;
}

static int read_rejects_uninitialized_or_missing_device(void)
{
    bh1750_t sensor = {0};
    float lux = 42.0f;
    CHECK(bh1750_read_lux(&sensor, &lux) == ESP_ERR_INVALID_STATE);
    sensor.i2c_dev = DEVICE;
    CHECK(bh1750_read_lux(&sensor, &lux) == ESP_ERR_INVALID_STATE);
    sensor.i2c_dev = NULL;
    sensor.initialized = true;
    CHECK(bh1750_read_lux(&sensor, &lux) == ESP_ERR_INVALID_STATE);
    CHECK(lux == 42.0f && event_count == 0);
    return 0;
}

static int read_waits_and_receives_big_endian(void)
{
    bh1750_t sensor = {.i2c_dev = DEVICE, .initialized = true};
    float lux = -1.0f;
    CHECK(bh1750_read_lux(&sensor, &lux) == ESP_OK);
    CHECK(lux > 239.99f && lux < 240.01f); /* 0x0120 = 288 -> 240 lux */
    CHECK(event_count == 4);
    CHECK(events[0].kind == TRANSMIT && events[0].value == 0x01);
    CHECK(events[1].kind == TRANSMIT && events[1].value == 0x20);
    CHECK(events[2].kind == DELAY);
    CHECK(events[3].kind == RECEIVE && events[3].value == 2);
    CHECK(sensor.initialized && sensor.i2c_dev == DEVICE);
    return 0;
}

static int read_datasheet_example(void)
{
    bh1750_t sensor = {.i2c_dev = DEVICE, .initialized = true};
    response[0] = 0x83;
    response[1] = 0x90;
    float lux = 0;
    CHECK(bh1750_read_lux(&sensor, &lux) == ESP_OK);
    CHECK(lux > 28066.6f && lux < 28066.8f);
    return 0;
}

static int read_zero_is_valid(void)
{
    bh1750_t sensor = {.i2c_dev = DEVICE, .initialized = true};
    response[0] = response[1] = 0;
    float lux = -1;
    CHECK(bh1750_read_lux(&sensor, &lux) == ESP_OK && lux == 0.0f);
    return 0;
}

static int read_full_scale_is_unsigned(void)
{
    bh1750_t sensor = {.i2c_dev = DEVICE, .initialized = true};
    response[0] = response[1] = 0xFF;
    float lux = -1;
    CHECK(bh1750_read_lux(&sensor, &lux) == ESP_OK);
    CHECK(lux > 54612.4f && lux < 54612.6f);
    return 0;
}

static int read_retains_fractional_lux(void)
{
    bh1750_t sensor = {.i2c_dev = DEVICE, .initialized = true};
    response[0] = 0;
    response[1] = 1;
    float lux = 0;
    CHECK(bh1750_read_lux(&sensor, &lux) == ESP_OK);
    CHECK(lux > 0.8333f && lux < 0.8334f);
    return 0;
}

static int read_power_on_failure_preserves_output(void)
{
    bh1750_t sensor = {.i2c_dev = DEVICE, .initialized = true};
    fail_transmit = 1;
    float lux = 987.0f;
    CHECK(bh1750_read_lux(&sensor, &lux) == ESP_ERR_TIMEOUT);
    CHECK(lux == 987.0f && event_count == 1 && wait_ticks == 0);
    CHECK(sensor.initialized);
    return 0;
}

static int read_mode_failure_preserves_output(void)
{
    bh1750_t sensor = {.i2c_dev = DEVICE, .initialized = true};
    fail_transmit = 2;
    transmit_result = ESP_FAIL;
    float lux = 987.0f;
    CHECK(bh1750_read_lux(&sensor, &lux) == ESP_FAIL);
    CHECK(lux == 987.0f && event_count == 2 && wait_ticks == 0);
    return 0;
}

static int read_receive_failure_preserves_output_and_can_retry(void)
{
    bh1750_t sensor = {.i2c_dev = DEVICE, .initialized = true};
    receive_result = ESP_ERR_TIMEOUT;
    float lux = 987.0f;
    CHECK(bh1750_read_lux(&sensor, &lux) == ESP_ERR_TIMEOUT);
    CHECK(lux == 987.0f && event_count == 4);
    receive_result = ESP_OK;
    CHECK(bh1750_read_lux(&sensor, &lux) == ESP_OK);
    CHECK(lux > 239.99f && lux < 240.01f);
    return 0;
}

static int each_read_starts_new_measurement(void)
{
    bh1750_t sensor = {.i2c_dev = DEVICE, .initialized = true};
    float lux = 0;
    CHECK(bh1750_read_lux(&sensor, &lux) == ESP_OK);
    response[0] = 0x02;
    response[1] = 0x40;
    wait_ticks = 0;
    CHECK(bh1750_read_lux(&sensor, &lux) == ESP_OK);
    CHECK(lux > 479.99f && lux < 480.01f && event_count == 8);
    CHECK(events[4].kind == TRANSMIT && events[4].value == 0x01);
    CHECK(events[5].kind == TRANSMIT && events[5].value == 0x20);
    CHECK(events[6].kind == DELAY && events[7].kind == RECEIVE);
    return 0;
}

static int deinit_rejects_invalid_state(void)
{
    bh1750_t sensor = {0};
    CHECK(bh1750_deinit(NULL) == ESP_ERR_INVALID_ARG);
    CHECK(bh1750_deinit(&sensor) == ESP_ERR_INVALID_STATE);
    CHECK(event_count == 0);
    return 0;
}

static int deinit_removes_only_device(void)
{
    bh1750_t sensor = {.i2c_dev = DEVICE, .initialized = true};
    CHECK(bh1750_deinit(&sensor) == ESP_OK);
    CHECK(sensor.i2c_dev == NULL && !sensor.initialized);
    CHECK(event_count == 1 && events[0].kind == REMOVE);
    CHECK(bh1750_deinit(&sensor) == ESP_ERR_INVALID_STATE && event_count == 1);
    return 0;
}

static int deinit_failure_retains_state_for_retry(void)
{
    bh1750_t sensor = {.i2c_dev = DEVICE, .initialized = true};
    remove_result = ESP_FAIL;
    CHECK(bh1750_deinit(&sensor) == ESP_FAIL);
    CHECK(sensor.i2c_dev == DEVICE && sensor.initialized);
    remove_result = ESP_OK;
    CHECK(bh1750_deinit(&sensor) == ESP_OK);
    CHECK(sensor.i2c_dev == NULL && !sensor.initialized && event_count == 2);
    return 0;
}

#define TEST(function) {#function, function}
static const struct { const char *name; int (*run)(void); } tests[] = {
    TEST(init_default_address_sequence), TEST(init_alternate_address_sequence),
    TEST(init_rejects_invalid_arguments), TEST(init_rejects_duplicate_without_bus_access),
    TEST(init_add_failure_is_retryable), TEST(every_init_command_failure_cleans_up),
    TEST(init_cleanup_failure_preserves_handle_for_retry), TEST(init_command_failure_can_retry_after_cleanup),
    TEST(read_rejects_null_arguments_preserving_output), TEST(read_rejects_uninitialized_or_missing_device),
    TEST(read_waits_and_receives_big_endian), TEST(read_datasheet_example), TEST(read_zero_is_valid),
    TEST(read_full_scale_is_unsigned), TEST(read_retains_fractional_lux),
    TEST(read_power_on_failure_preserves_output), TEST(read_mode_failure_preserves_output),
    TEST(read_receive_failure_preserves_output_and_can_retry), TEST(each_read_starts_new_measurement),
    TEST(deinit_rejects_invalid_state), TEST(deinit_removes_only_device),
    TEST(deinit_failure_retains_state_for_retry),
};
int bh1750_test_count(void) { return (int)COUNT(tests); }
const char *bh1750_test_name(int index)
{ return index >= 0 && index < bh1750_test_count() ? tests[index].name : "invalid_index"; }
int bh1750_test_run(int index)
{
    if (index < 0 || index >= bh1750_test_count()) return __LINE__;
    reset_mock();
    int result = tests[index].run();
    return result ? result : mock_fault;
}
