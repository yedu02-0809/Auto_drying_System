/* Exercise the production DS3231 driver against a register-level I2C mock. */
#include <stddef.h>
#include <string.h>
#include "rtc_ds3231.h"

#define CHECK(condition) do { if (!(condition)) return __LINE__; } while (0)
#define COUNT(array) (sizeof(array) / sizeof((array)[0]))

struct mock_i2c_bus { int unused; };
struct mock_i2c_device { int unused; };
static struct mock_i2c_bus bus_object;
static struct mock_i2c_device device_object;
static uint8_t registers[0x13];
static int add_calls, remove_calls, read_calls, write_calls, mock_fault;
static int fail_read, fail_write;
static esp_err_t add_result, remove_result;
static struct { char kind; uint8_t address; size_t length; } events[32];
static size_t event_count;

static void reset_mock(void)
{
    const uint8_t time[] = {0x56, 0x34, 0x18, 2, 0x21, 0x09, 0x26};
    memset(registers, 0, sizeof(registers));
    memcpy(registers, time, sizeof(time));
    registers[0x0E] = 0x1C;
    registers[0x0F] = 0x08;
    add_calls = remove_calls = read_calls = write_calls = mock_fault = 0;
    fail_read = fail_write = 0;
    add_result = remove_result = ESP_OK;
    event_count = 0;
}

static void record_event(char kind, uint8_t address, size_t length)
{
    if (event_count >= COUNT(events)) {
        mock_fault = __LINE__;
        return;
    }
    events[event_count].kind = kind;
    events[event_count].address = address;
    events[event_count++].length = length;
}

esp_err_t i2c_master_bus_add_device(i2c_master_bus_handle_t bus,
                                  const i2c_device_config_t *config,
                                  i2c_master_dev_handle_t *device)
{
    ++add_calls;
    if (bus != &bus_object || config == NULL || device == NULL ||
        config->dev_addr_length != I2C_ADDR_BIT_LEN_7 ||
        config->device_address != 0x68 || config->scl_speed_hz != 100000) {
        mock_fault = __LINE__;
        return ESP_FAIL;
    }
    if (add_result == ESP_OK) *device = &device_object;
    return add_result;
}

esp_err_t i2c_master_bus_rm_device(i2c_master_dev_handle_t device)
{
    ++remove_calls;
    if (device != &device_object) mock_fault = __LINE__;
    return remove_result;
}

esp_err_t i2c_master_transmit_receive(i2c_master_dev_handle_t device,
                                   const uint8_t *write_data, size_t write_length,
                                   uint8_t *read_data, size_t read_length, int timeout_ms)
{
    ++read_calls;
    if (device != &device_object || write_data == NULL || write_length != 1 ||
        read_data == NULL || read_length == 0 || timeout_ms != 100 ||
        (size_t)write_data[0] + read_length > sizeof(registers)) {
        mock_fault = __LINE__;
        return ESP_FAIL;
    }
    record_event('R', write_data[0], read_length);
    if (read_calls == fail_read) return ESP_ERR_TIMEOUT;
    memcpy(read_data, registers + write_data[0], read_length);
    return ESP_OK;
}

esp_err_t i2c_master_transmit(i2c_master_dev_handle_t device,
                           const uint8_t *data, size_t length, int timeout_ms)
{
    ++write_calls;
    if (device != &device_object || data == NULL || length < 2 || timeout_ms != 100 ||
        (size_t)data[0] + length - 1 > sizeof(registers)) {
        mock_fault = __LINE__;
        return ESP_FAIL;
    }
    record_event('W', data[0], length - 1);
    if (write_calls == fail_write) return ESP_FAIL;
    memcpy(registers + data[0], data + 1, length - 1);
    return ESP_OK;
}

static int failed_read_preserves_output(esp_err_t expected)
{
    rtc_t rtc = {.i2c_dev = &device_object};
    rtc_datetime_t output, original;
    memset(&output, 0xA5, sizeof(output));
    memcpy(&original, &output, sizeof(output));
    CHECK(rtc_ds3231_get_datetime(&rtc, &output) == expected);
    CHECK(memcmp(&output, &original, sizeof(output)) == 0);
    CHECK(write_calls == 0);
    return 0;
}

static int init_keeps_configuration(void)
{
    rtc_t rtc = {0};
    uint8_t before[sizeof(registers)];
    registers[0x0F] = 0x88;
    memcpy(before, registers, sizeof(before));
    CHECK(rtc_ds3231_init(&rtc, &bus_object) == ESP_OK);
    CHECK(rtc.i2c_dev == &device_object && add_calls == 1);
    CHECK(read_calls == 1 && write_calls == 0 && remove_calls == 0);
    CHECK(events[0].address == 0x0E && events[0].length == 1);
    CHECK(memcmp(before, registers, sizeof(before)) == 0);
    return 0;
}

static int init_enables_oscillator(void)
{
    rtc_t rtc = {0};
    registers[0x0E] = 0xDF;
    registers[0x0F] = 0x8B;
    CHECK(rtc_ds3231_init(&rtc, &bus_object) == ESP_OK);
    CHECK(registers[0x0E] == 0x5F && registers[0x0F] == 0x8B);
    CHECK(read_calls == 1 && write_calls == 1);
    CHECK(events[1].kind == 'W' && events[1].address == 0x0E && events[1].length == 1);
    return 0;
}

static int init_arguments(void)
{
    rtc_t rtc = {0};
    CHECK(rtc_ds3231_init(NULL, &bus_object) == ESP_ERR_INVALID_ARG);
    CHECK(rtc_ds3231_init(&rtc, NULL) == ESP_ERR_INVALID_ARG);
    CHECK(add_calls == 0);
    CHECK(rtc_ds3231_init(&rtc, &bus_object) == ESP_OK);
    CHECK(rtc_ds3231_init(&rtc, &bus_object) == ESP_ERR_INVALID_STATE);
    CHECK(add_calls == 1 && read_calls == 1);
    return 0;
}

static int init_add_failure(void)
{
    rtc_t rtc = {0};
    add_result = ESP_ERR_NO_MEM;
    CHECK(rtc_ds3231_init(&rtc, &bus_object) == ESP_ERR_NO_MEM);
    CHECK(rtc.i2c_dev == NULL && read_calls == 0 && remove_calls == 0);
    add_result = ESP_OK;
    CHECK(rtc_ds3231_init(&rtc, &bus_object) == ESP_OK);
    return 0;
}

static int init_read_failure_cleanup(void)
{
    rtc_t rtc = {0};
    fail_read = 1;
    CHECK(rtc_ds3231_init(&rtc, &bus_object) == ESP_ERR_TIMEOUT);
    CHECK(rtc.i2c_dev == NULL && remove_calls == 1 && write_calls == 0);
    fail_read = 0;
    CHECK(rtc_ds3231_init(&rtc, &bus_object) == ESP_OK);
    return 0;
}

static int init_write_failure_cleanup(void)
{
    rtc_t rtc = {0};
    registers[0x0E] = 0x9C;
    fail_write = 1;
    CHECK(rtc_ds3231_init(&rtc, &bus_object) == ESP_FAIL);
    CHECK(rtc.i2c_dev == NULL && remove_calls == 1 && registers[0x0E] == 0x9C);
    fail_write = 0;
    CHECK(rtc_ds3231_init(&rtc, &bus_object) == ESP_OK);
    CHECK(registers[0x0E] == 0x1C);
    return 0;
}

static int init_cleanup_failure_retry(void)
{
    rtc_t rtc = {0};
    fail_read = 1;
    remove_result = ESP_FAIL;
    CHECK(rtc_ds3231_init(&rtc, &bus_object) == ESP_ERR_TIMEOUT);
    CHECK(rtc.i2c_dev == &device_object && remove_calls == 1);
    CHECK(rtc_ds3231_init(&rtc, &bus_object) == ESP_ERR_INVALID_STATE && add_calls == 1);
    remove_result = ESP_OK;
    CHECK(rtc_ds3231_deinit(&rtc) == ESP_OK && rtc.i2c_dev == NULL);
    CHECK(rtc_ds3231_init(&rtc, &bus_object) == ESP_OK);
    return 0;
}

static int read_24h_snapshot(void)
{
    rtc_t rtc = {.i2c_dev = &device_object};
    rtc_datetime_t output = {0};
    CHECK(rtc_ds3231_get_datetime(&rtc, &output) == ESP_OK);
    CHECK(output.year == 2026 && output.month == 9 && output.day == 21);
    CHECK(output.hour == 18 && output.minute == 34 && output.second == 56);
    CHECK(event_count == 2 && events[0].kind == 'R' && events[0].address == 0);
    CHECK(events[0].length == 7 && events[1].address == 0x0F && events[1].length == 1);
    CHECK(write_calls == 0);
    return 0;
}

static int read_12h_conversion(void)
{
    const uint8_t raw[] = {0x52, 0x72, 0x41, 0x61, 0x51, 0x71};
    const uint8_t expected[] = {0, 12, 1, 13, 11, 23};
    rtc_t rtc = {.i2c_dev = &device_object};
    for (size_t i = 0; i < COUNT(raw); ++i) {
        rtc_datetime_t output = {0};
        registers[2] = raw[i];
        CHECK(rtc_ds3231_get_datetime(&rtc, &output) == ESP_OK && output.hour == expected[i]);
    }
    return 0;
}

static int read_osf(void)
{
    registers[0x0F] = 0x8F;
    CHECK(failed_read_preserves_output(ESP_ERR_INVALID_STATE) == 0);
    CHECK(registers[0x0F] == 0x8F);
    return 0;
}

static int read_i2c_errors(void)
{
    for (int call = 1; call <= 2; ++call) {
        reset_mock();
        fail_read = call;
        CHECK(failed_read_preserves_output(ESP_ERR_TIMEOUT) == 0);
        CHECK(read_calls == call);
    }
    return 0;
}

static int read_bad_bcd(void)
{
    const uint8_t offsets[] = {0, 1, 2, 4, 5, 6, 6};
    const uint8_t values[] = {0x1A, 0x2F, 0x1B, 0x1A, 0x0A, 0x9A, 0xA0};
    for (size_t i = 0; i < COUNT(offsets); ++i) {
        reset_mock();
        registers[offsets[i]] = values[i];
        CHECK(failed_read_preserves_output(ESP_ERR_INVALID_RESPONSE) == 0);
    }
    return 0;
}

static int read_illegal_dates(void)
{
    const uint8_t offsets[] = {0, 1, 2, 2, 2, 3, 3, 4, 4, 5, 5};
    const uint8_t values[] = {0x60, 0x60, 0x24, 0x40, 0x53, 0, 8, 0, 0x31, 0, 0x13};
    for (size_t i = 0; i < COUNT(offsets); ++i) {
        reset_mock();
        registers[offsets[i]] = values[i];
        CHECK(failed_read_preserves_output(ESP_ERR_INVALID_RESPONSE) == 0);
    }
    reset_mock();
    registers[4] = 0x29;
    registers[5] = 0x02;
    CHECK(failed_read_preserves_output(ESP_ERR_INVALID_RESPONSE) == 0);
    registers[6] = 0x24;
    rtc_t rtc = {.i2c_dev = &device_object};
    rtc_datetime_t output = {0};
    CHECK(rtc_ds3231_get_datetime(&rtc, &output) == ESP_OK && output.day == 29);
    registers[6] = 0;
    CHECK(rtc_ds3231_get_datetime(&rtc, &output) == ESP_OK && output.year == 2000);
    return 0;
}

static int read_reserved_bits(void)
{
    const uint8_t offsets[] = {0, 1, 2, 3, 4, 4, 5, 5, 5};
    const uint8_t bits[] = {0x80, 0x80, 0x80, 0x08, 0x40, 0x80, 0x20, 0x40, 0x80};
    for (size_t i = 0; i < COUNT(offsets); ++i) {
        reset_mock();
        registers[offsets[i]] |= bits[i];
        CHECK(failed_read_preserves_output(ESP_ERR_INVALID_RESPONSE) == 0);
    }
    return 0;
}

static int set_bcd_weekday_status(void)
{
    rtc_t rtc = {.i2c_dev = &device_object};
    const rtc_datetime_t input = {2026, 9, 21, 18, 7, 59};
    const uint8_t expected[] = {0x59, 0x07, 0x18, 2, 0x21, 0x09, 0x26};
    registers[0x0F] = 0x8F;
    registers[7] = 0xAB;
    registers[0x0E] = 0x5F;
    CHECK(rtc_ds3231_set_datetime(&rtc, &input) == ESP_OK);
    CHECK(memcmp(registers, expected, sizeof(expected)) == 0);
    CHECK(registers[0x0F] == 0x0F && registers[7] == 0xAB && registers[0x0E] == 0x5F);
    CHECK(event_count == 3 && events[0].kind == 'W' && events[0].address == 0);
    CHECK(events[0].length == 7 && events[1].kind == 'R' && events[1].address == 0x0F);
    CHECK(events[2].kind == 'W' && events[2].address == 0x0F && events[2].length == 1);
    return 0;
}

static int set_calendar_boundaries(void)
{
    const rtc_datetime_t dates[] = {
        {2000, 1, 1, 0, 0, 0}, {2000, 2, 29, 12, 0, 0},
        {2024, 2, 29, 23, 59, 59}, {2026, 9, 20, 18, 0, 0},
        {2099, 12, 31, 23, 59, 59},
    };
    const uint8_t weekdays[] = {7, 3, 5, 1, 5};
    rtc_t rtc = {.i2c_dev = &device_object};
    for (size_t i = 0; i < COUNT(dates); ++i) {
        reset_mock();
        CHECK(rtc_ds3231_set_datetime(&rtc, &dates[i]) == ESP_OK);
        CHECK(registers[3] == weekdays[i] && (registers[2] & 0x40) == 0);
        CHECK((registers[5] & 0x80) == 0);
        rtc_datetime_t output = {0};
        CHECK(rtc_ds3231_get_datetime(&rtc, &output) == ESP_OK);
        CHECK(output.year == dates[i].year && output.month == dates[i].month);
        CHECK(output.day == dates[i].day && output.hour == dates[i].hour);
        CHECK(output.minute == dates[i].minute && output.second == dates[i].second);
    }
    return 0;
}

static int set_invalid_arguments(void)
{
    const rtc_datetime_t dates[] = {
        {1999, 1, 1, 0, 0, 0}, {2100, 1, 1, 0, 0, 0},
        {2026, 0, 1, 0, 0, 0}, {2026, 13, 1, 0, 0, 0},
        {2026, 1, 0, 0, 0, 0}, {2026, 1, 32, 0, 0, 0},
        {2026, 2, 29, 0, 0, 0}, {2024, 2, 30, 0, 0, 0},
        {2026, 4, 31, 0, 0, 0}, {2026, 1, 1, 24, 0, 0},
        {2026, 1, 1, 0, 60, 0}, {2026, 1, 1, 0, 0, 60},
    };
    rtc_t rtc = {.i2c_dev = &device_object};
    for (size_t i = 0; i < COUNT(dates); ++i) {
        CHECK(rtc_ds3231_set_datetime(&rtc, &dates[i]) == ESP_ERR_INVALID_ARG);
    }
    CHECK(read_calls == 0 && write_calls == 0);
    return 0;
}

static int set_i2c_errors(void)
{
    rtc_t rtc = {.i2c_dev = &device_object};
    const rtc_datetime_t input = {2026, 9, 21, 18, 0, 0};
    fail_write = 1;
    CHECK(rtc_ds3231_set_datetime(&rtc, &input) == ESP_FAIL && read_calls == 0);
    reset_mock();
    registers[0x0F] = 0x88;
    fail_read = 1;
    CHECK(rtc_ds3231_set_datetime(&rtc, &input) == ESP_ERR_TIMEOUT && write_calls == 1);
    CHECK(registers[0x0F] == 0x88);
    reset_mock();
    registers[0x0F] = 0x88;
    fail_write = 2;
    CHECK(rtc_ds3231_set_datetime(&rtc, &input) == ESP_FAIL && write_calls == 2);
    CHECK(registers[0x0F] == 0x88);
    fail_write = 0;
    CHECK(rtc_ds3231_set_datetime(&rtc, &input) == ESP_OK && registers[0x0F] == 0x08);
    return 0;
}

static int api_state_validation(void)
{
    rtc_t rtc = {0};
    rtc_datetime_t datetime = {2026, 9, 21, 18, 0, 0};
    CHECK(rtc_ds3231_get_datetime(NULL, &datetime) == ESP_ERR_INVALID_ARG);
    CHECK(rtc_ds3231_get_datetime(&rtc, NULL) == ESP_ERR_INVALID_ARG);
    CHECK(rtc_ds3231_get_datetime(&rtc, &datetime) == ESP_ERR_INVALID_STATE);
    CHECK(rtc_ds3231_set_datetime(NULL, &datetime) == ESP_ERR_INVALID_ARG);
    CHECK(rtc_ds3231_set_datetime(&rtc, NULL) == ESP_ERR_INVALID_ARG);
    CHECK(rtc_ds3231_set_datetime(&rtc, &datetime) == ESP_ERR_INVALID_STATE);
    CHECK(rtc_ds3231_deinit(NULL) == ESP_ERR_INVALID_ARG);
    CHECK(rtc_ds3231_deinit(&rtc) == ESP_ERR_INVALID_STATE);
    CHECK(read_calls == 0 && write_calls == 0 && remove_calls == 0);
    return 0;
}

static int deinit_retry(void)
{
    rtc_t rtc = {.i2c_dev = &device_object};
    remove_result = ESP_FAIL;
    CHECK(rtc_ds3231_deinit(&rtc) == ESP_FAIL && rtc.i2c_dev == &device_object);
    remove_result = ESP_OK;
    CHECK(rtc_ds3231_deinit(&rtc) == ESP_OK && rtc.i2c_dev == NULL);
    CHECK(remove_calls == 2 && read_calls == 0 && write_calls == 0);
    CHECK(rtc_ds3231_deinit(&rtc) == ESP_ERR_INVALID_STATE && remove_calls == 2);
    return 0;
}

#define TEST(function) {#function, function}
static const struct { const char *name; int (*run)(void); } tests[] = {
    TEST(init_keeps_configuration), TEST(init_enables_oscillator), TEST(init_arguments),
    TEST(init_add_failure), TEST(init_read_failure_cleanup), TEST(init_write_failure_cleanup),
    TEST(init_cleanup_failure_retry), TEST(read_24h_snapshot), TEST(read_12h_conversion),
    TEST(read_osf), TEST(read_i2c_errors), TEST(read_bad_bcd), TEST(read_illegal_dates),
    TEST(read_reserved_bits), TEST(set_bcd_weekday_status), TEST(set_calendar_boundaries),
    TEST(set_invalid_arguments), TEST(set_i2c_errors), TEST(api_state_validation), TEST(deinit_retry),
};

int rtc_test_count(void) { return (int)COUNT(tests); }
const char *rtc_test_name(int index)
{
    return index >= 0 && index < rtc_test_count() ? tests[index].name : "invalid_index";
}
int rtc_test_run(int index)
{
    if (index < 0 || index >= rtc_test_count()) return __LINE__;
    reset_mock();
    int result = tests[index].run();
    return result ? result : mock_fault;
}
