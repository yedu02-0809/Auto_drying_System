/* Test the OLED screen composition and full render path with a mock I2C bus. */
#include <stddef.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "display.h"
#include "ssd1306.h"
#include "ssd1306_font.h"

#define CHECK(condition) do { if (!(condition)) return __LINE__; } while (0)

struct mock_i2c_bus { int unused; };
struct mock_i2c_device { int unused; };
static struct mock_i2c_bus bus;
static struct mock_i2c_device device;
static esp_err_t add_result, rm_result, tx_result;
static int add_calls, rm_calls, tx_calls, mock_fault;

static void reset_mock(void)
{
    add_result = rm_result = tx_result = ESP_OK;
    add_calls = rm_calls = tx_calls = mock_fault = 0;
}

esp_err_t i2c_master_bus_add_device(i2c_master_bus_handle_t handle,
                                    const i2c_device_config_t *config,
                                    i2c_master_dev_handle_t *out)
{
    ++add_calls;
    if (handle != &bus || config == NULL || out == NULL) {
        mock_fault = __LINE__;
        return ESP_ERR_INVALID_ARG;
    }
    if (add_result == ESP_OK) {
        *out = &device;
    }
    return add_result;
}

esp_err_t i2c_master_bus_rm_device(i2c_master_dev_handle_t handle)
{
    ++rm_calls;
    if (handle != &device) {
        mock_fault = __LINE__;
        return ESP_ERR_INVALID_ARG;
    }
    return rm_result;
}

esp_err_t i2c_master_transmit(i2c_master_dev_handle_t handle, const uint8_t *data,
                              size_t length, int timeout_ms)
{
    (void)data; (void)length; (void)timeout_ms;
    if (handle != &device) {
        mock_fault = __LINE__;
        return ESP_ERR_INVALID_ARG;
    }
    ++tx_calls;
    return tx_result;
}

esp_err_t i2c_master_receive(i2c_master_dev_handle_t handle, uint8_t *data,
                             size_t length, int timeout_ms)
{
    (void)handle; (void)data; (void)length; (void)timeout_ms;
    mock_fault = __LINE__;
    return ESP_FAIL;
}

esp_err_t i2c_master_transmit_receive(i2c_master_dev_handle_t handle, const uint8_t *write_data,
                                      size_t write_length, uint8_t *read_data, size_t read_length,
                                      int timeout_ms)
{
    (void)handle; (void)write_data; (void)write_length; (void)read_data; (void)read_length;
    (void)timeout_ms;
    mock_fault = __LINE__;
    return ESP_FAIL;
}

void vTaskDelay(TickType_t ticks) { (void)ticks; }

const char *esp_err_to_name(esp_err_t error) { (void)error; return "mock_error"; }

void mock_log(const char *tag, const char *format, ...) { (void)tag; (void)format; }

static web_status_t sample_status(void)
{
    web_status_t status = {0};
    status.time_valid = true;
    status.year = 2026;
    status.month = 5;
    status.day = 1;
    status.hour = 12;
    status.minute = 34;
    status.second = 56;
    status.rain_valid = true;
    status.raining = false;
    status.climate_valid = true;
    status.temperature_c = 23.5f;
    status.humidity_percent = 41.5f;
    status.light_valid = true;
    status.lux = 1234.5f;
    status.position = 2; /* retracted */
    status.mode = 0;     /* auto */
    status.wifi_connected = true;
    strcpy(status.ip, "192.168.1.10");
    return status;
}

static int composes_four_full_lines(void)
{
    const web_status_t status = sample_status();
    char lines[DISPLAY_LINES][DISPLAY_COLUMNS + 1];
    display_compose(&status, lines);
    CHECK(strcmp(lines[0], "05-01 12:34 AUTO") == 0);
    CHECK(strcmp(lines[1], "23.5C 41.5%RH") == 0);
    CHECK(strcmp(lines[2], "1235lx RAIN:NO") == 0);
    CHECK(strcmp(lines[3], "RACK:RETRACT W") == 0);
    return 0;
}

static int composes_missing_rtc_and_sensors(void)
{
    web_status_t status = {0};
    char lines[DISPLAY_LINES][DISPLAY_COLUMNS + 1];
    display_compose(&status, lines);
    CHECK(strcmp(lines[0], "NO RTC  AUTO") == 0);
    CHECK(strcmp(lines[1], "NO CLIMATE DATA") == 0);
    CHECK(strcmp(lines[2], "----lx RAIN:--") == 0);
    CHECK(strcmp(lines[3], "RACK:UNKNOWN") == 0);

    status.mode = 1;
    display_compose(&status, lines);
    CHECK(strcmp(lines[0], "NO RTC  MAN") == 0);
    return 0;
}

static int composes_every_rack_state(void)
{
    static const struct { int position; const char *text; } states[] = {
        { 0, "RACK:UNKNOWN" }, { 1, "RACK:EXTEND" }, { 2, "RACK:RETRACT" },
        { 3, "RACK:MOV:EXT" }, { 4, "RACK:MOV:RET" }, { 9, "RACK:UNKNOWN" },
    };
    char lines[DISPLAY_LINES][DISPLAY_COLUMNS + 1];
    for (size_t i = 0; i < sizeof(states) / sizeof(states[0]); ++i) {
        web_status_t status = sample_status();
        status.wifi_connected = false;
        status.position = states[i].position;
        display_compose(&status, lines);
        CHECK(strcmp(lines[3], states[i].text) == 0);
    }
    return 0;
}

static int composes_rain_and_clamped_lux(void)
{
    web_status_t status = sample_status();
    char lines[DISPLAY_LINES][DISPLAY_COLUMNS + 1];
    status.raining = true;
    display_compose(&status, lines);
    CHECK(strcmp(lines[2], "1235lx RAIN:YES") == 0);

    status.lux = 200000.0f;
    display_compose(&status, lines);
    CHECK(strcmp(lines[2], "99999lx RAIN:YES") == 0);

    status.light_valid = false;
    display_compose(&status, lines);
    CHECK(strcmp(lines[2], "----lx RAIN:YES") == 0);
    return 0;
}

static int composes_extreme_climate_values(void)
{
    web_status_t status = sample_status();
    char lines[DISPLAY_LINES][DISPLAY_COLUMNS + 1];
    status.temperature_c = -5.0f;
    status.humidity_percent = 100.0f;
    display_compose(&status, lines);
    CHECK(strcmp(lines[1], "-5.0C 100.0%RH") == 0);
    return 0;
}

static int compose_rejects_null_arguments(void)
{
    char lines[DISPLAY_LINES][DISPLAY_COLUMNS + 1];
    memset(lines, 'x', sizeof(lines));
    display_compose(NULL, lines);
    CHECK(lines[0][0] == 'x');

    const web_status_t status = sample_status();
    display_compose(&status, NULL);
    return 0;
}

static int render_clears_draws_and_flushes(void)
{
    ssd1306_t display = {0};
    CHECK(ssd1306_init(&display, &bus, SSD1306_ADDRESS_DEFAULT) == ESP_OK);
    const int tx_after_init = tx_calls;
    CHECK(tx_after_init == 17);

    /* 先塞一点脏数据，确认 render 会先清屏。 */
    memset(display.buffer, 0xFF, sizeof(display.buffer));
    const web_status_t status = sample_status();
    CHECK(display_render(&display, &status) == ESP_OK);
    CHECK(tx_calls == tx_after_init + 16);

    /* 第 0 行第 0 列应当是 '0'。 */
    const uint8_t *zero = ssd1306_font8x16['0' - 0x20];
    CHECK(memcmp(&display.buffer[0], zero, 8) == 0);
    CHECK(memcmp(&display.buffer[SSD1306_WIDTH], zero + 8, 8) == 0);
    /* 第 15 列之后的一列保持清屏状态（不会被上一帧残留）。 */
    CHECK(display.buffer[128] == 0x00);
    return 0;
}

static int render_propagates_flush_errors(void)
{
    ssd1306_t display = {0};
    CHECK(ssd1306_init(&display, &bus, SSD1306_ADDRESS_DEFAULT) == ESP_OK);
    tx_result = ESP_FAIL;
    const web_status_t status = sample_status();
    CHECK(display_render(&display, &status) == ESP_FAIL);

    tx_result = ESP_OK;
    CHECK(display_render(&display, &status) == ESP_OK);
    CHECK(display_render(NULL, &status) == ESP_ERR_INVALID_ARG);
    CHECK(display_render(&display, NULL) == ESP_ERR_INVALID_ARG);
    return 0;
}

struct test_case { const char *name; int (*run)(void); };
#define TEST(name) {#name, name}
static const struct test_case cases[] = {
    TEST(composes_four_full_lines),
    TEST(composes_missing_rtc_and_sensors),
    TEST(composes_every_rack_state),
    TEST(composes_rain_and_clamped_lux),
    TEST(composes_extreme_climate_values),
    TEST(compose_rejects_null_arguments),
    TEST(render_clears_draws_and_flushes),
    TEST(render_propagates_flush_errors),
};

int display_test_count(void) { return (int)(sizeof(cases) / sizeof(cases[0])); }
const char *display_test_name(int index) { return cases[index].name; }
int display_test_run(int index)
{
    reset_mock();
    int line = cases[index].run();
    return line ? line : mock_fault;
}
