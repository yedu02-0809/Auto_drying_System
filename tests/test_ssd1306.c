/* Test production ssd1306.c against a mock I2C bus: no OLED hardware needed. */
#include <stddef.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "ssd1306.h"
#include "ssd1306_font.h"

#define CHECK(condition) do { if (!(condition)) return __LINE__; } while (0)

struct mock_i2c_bus { int unused; };
struct mock_i2c_device { int unused; };
static struct mock_i2c_bus bus;
static struct mock_i2c_device device;

#define CAPTURE_LIMIT 64
static uint8_t captures[CAPTURE_LIMIT][1 + SSD1306_WIDTH];
static size_t capture_lengths[CAPTURE_LIMIT];
static i2c_device_config_t last_config;
static esp_err_t add_result, rm_result, tx_result;
static int add_calls, rm_calls, tx_calls, delay_calls, fail_tx_on, mock_fault;
static TickType_t last_delay;
static int64_t now_us;

static void reset_mock(void)
{
    memset(captures, 0, sizeof(captures));
    memset(capture_lengths, 0, sizeof(capture_lengths));
    memset(&last_config, 0, sizeof(last_config));
    add_result = rm_result = tx_result = ESP_OK;
    add_calls = rm_calls = tx_calls = delay_calls = fail_tx_on = mock_fault = 0;
    last_delay = 0;
    now_us = 0;
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
    last_config = *config;
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
    (void)timeout_ms;
    if (handle != &device || data == NULL || length == 0 || length > 1 + SSD1306_WIDTH) {
        mock_fault = __LINE__;
        return ESP_ERR_INVALID_ARG;
    }
    ++tx_calls;
    if (fail_tx_on != 0 && tx_calls == fail_tx_on) {
        return ESP_ERR_TIMEOUT;
    }
    if (tx_result != ESP_OK) {
        return tx_result;
    }
    if (tx_calls <= CAPTURE_LIMIT) {
        memcpy(captures[tx_calls - 1], data, length);
        capture_lengths[tx_calls - 1] = length;
    }
    return ESP_OK;
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

void vTaskDelay(TickType_t ticks)
{
    ++delay_calls;
    last_delay = ticks;
}

const char *esp_err_to_name(esp_err_t error) { (void)error; return "mock_error"; }

void mock_log(const char *tag, const char *format, ...) { (void)tag; (void)format; }

/* 与 ssd1306.c 的 INIT_SEQUENCE 保持一致，用于逐字节核对下发的命令。 */
static const uint8_t EXPECTED_INIT[] = {
    0xAE,
    0xD5, 0x80,
    0xA8, 0x3F,
    0xD3, 0x00,
    0x40,
    0xA1,
    0xC8,
    0xDA, 0x12,
    0x81, 0xCF,
    0xD9, 0xF1,
    0xDB, 0x30,
    0xA4,
    0xA6,
    0x8D, 0x14,
    0xAF,
};

static int init_configures_device_and_command_sequence(void)
{
    ssd1306_t display = {0};
    CHECK(ssd1306_init(&display, &bus, SSD1306_ADDRESS_DEFAULT) == ESP_OK);
    CHECK(display.initialized && display.i2c_dev == &device);
    CHECK(add_calls == 1);
    CHECK(last_config.dev_addr_length == I2C_ADDR_BIT_LEN_7);
    CHECK(last_config.device_address == 0x3C);
    CHECK(last_config.scl_speed_hz == 400000);
    /* 100 ms 上电等待，100 Hz tick 下为 10 个 tick。 */
    CHECK(delay_calls == 1 && last_delay == pdMS_TO_TICKS(100));

    CHECK(tx_calls == 17);
    CHECK(capture_lengths[0] == 1 + sizeof(EXPECTED_INIT));
    CHECK(captures[0][0] == 0x00);
    CHECK(memcmp(&captures[0][1], EXPECTED_INIT, sizeof(EXPECTED_INIT)) == 0);
    return 0;
}

static int init_flushes_blank_screen_by_page(void)
{
    ssd1306_t display = {0};
    CHECK(ssd1306_init(&display, &bus, SSD1306_ADDRESS_DEFAULT) == ESP_OK);
    CHECK(tx_calls == 17);
    for (int page = 0; page < SSD1306_PAGES; ++page) {
        const uint8_t *commands = captures[1 + page * 2];
        const uint8_t *data = captures[2 + page * 2];
        CHECK(capture_lengths[1 + page * 2] == 4);
        CHECK(commands[0] == 0x00);
        CHECK(commands[1] == (uint8_t)(0xB0 | page));
        CHECK(commands[2] == 0x00);
        CHECK(commands[3] == 0x10);
        CHECK(capture_lengths[2 + page * 2] == 1 + SSD1306_WIDTH);
        CHECK(data[0] == 0x40);
        for (int i = 0; i < SSD1306_WIDTH; ++i) {
            CHECK(data[1 + i] == 0x00);
        }
    }
    return 0;
}

static int init_rejects_invalid_arguments(void)
{
    ssd1306_t display = {0};
    CHECK(ssd1306_init(NULL, &bus, SSD1306_ADDRESS_DEFAULT) == ESP_ERR_INVALID_ARG);
    CHECK(ssd1306_init(&display, NULL, SSD1306_ADDRESS_DEFAULT) == ESP_ERR_INVALID_ARG);
    CHECK(ssd1306_init(&display, &bus, 0x3E) == ESP_ERR_INVALID_ARG);
    CHECK(add_calls == 0 && tx_calls == 0);
    CHECK(!display.initialized && display.i2c_dev == NULL);

    CHECK(ssd1306_init(&display, &bus, SSD1306_ADDRESS_ALTERNATE) == ESP_OK);
    CHECK(last_config.device_address == 0x3D);
    CHECK(ssd1306_init(&display, &bus, SSD1306_ADDRESS_DEFAULT) == ESP_ERR_INVALID_STATE);
    return 0;
}

static int init_add_failure_keeps_object_clean(void)
{
    ssd1306_t display = {0};
    add_result = ESP_FAIL;
    CHECK(ssd1306_init(&display, &bus, SSD1306_ADDRESS_DEFAULT) == ESP_FAIL);
    CHECK(!display.initialized && display.i2c_dev == NULL);
    CHECK(tx_calls == 0 && rm_calls == 0);

    add_result = ESP_OK;
    CHECK(ssd1306_init(&display, &bus, SSD1306_ADDRESS_DEFAULT) == ESP_OK);
    CHECK(display.initialized);
    return 0;
}

static int init_command_failure_releases_device(void)
{
    ssd1306_t display = {0};
    fail_tx_on = 1;
    CHECK(ssd1306_init(&display, &bus, SSD1306_ADDRESS_DEFAULT) == ESP_ERR_TIMEOUT);
    CHECK(!display.initialized && display.i2c_dev == NULL);
    CHECK(rm_calls == 1);

    fail_tx_on = 0;
    CHECK(ssd1306_init(&display, &bus, SSD1306_ADDRESS_DEFAULT) == ESP_OK);
    CHECK(display.initialized && add_calls == 2);
    return 0;
}

static int init_retained_handle_is_cleaned_before_retry(void)
{
    ssd1306_t display = {.i2c_dev = &device, .initialized = false};
    CHECK(ssd1306_init(&display, &bus, SSD1306_ADDRESS_DEFAULT) == ESP_OK);
    CHECK(rm_calls == 1 && add_calls == 1);
    CHECK(display.initialized && display.i2c_dev == &device);
    return 0;
}

static int clear_zeroes_the_framebuffer(void)
{
    ssd1306_t display = {0};
    memset(display.buffer, 0xA5, sizeof(display.buffer));
    CHECK(ssd1306_clear(&display) == ESP_OK);
    for (size_t i = 0; i < sizeof(display.buffer); ++i) {
        CHECK(display.buffer[i] == 0x00);
    }
    CHECK(ssd1306_clear(NULL) == ESP_ERR_INVALID_ARG);
    return 0;
}

static int draw_text_places_glyph_bytes(void)
{
    ssd1306_t display = {0};
    CHECK(ssd1306_clear(&display) == ESP_OK);
    CHECK(ssd1306_draw_text(&display, 2, 3, "A") == ESP_OK);
    const uint8_t *glyph = ssd1306_font8x16['A' - 0x20];
    /* 第 2 行 = 页 4（上半）和页 5（下半），第 3 列 = x 24。 */
    CHECK(memcmp(&display.buffer[4 * SSD1306_WIDTH + 24], glyph, 8) == 0);
    CHECK(memcmp(&display.buffer[5 * SSD1306_WIDTH + 24], glyph + 8, 8) == 0);
    /* 前后邻居不受影响。 */
    CHECK(display.buffer[4 * SSD1306_WIDTH + 23] == 0x00);
    CHECK(display.buffer[4 * SSD1306_WIDTH + 32] == 0x00);
    return 0;
}

static int draw_text_clips_and_replaces_control_characters(void)
{
    ssd1306_t display = {0};
    CHECK(ssd1306_clear(&display) == ESP_OK);
    CHECK(ssd1306_draw_text(&display, 0, 0, "0123456789ABCDEFXX") == ESP_OK);
    /* 第 16 列（x=120）之后的内容被丢弃。 */
    CHECK(memcmp(&display.buffer[120], ssd1306_font8x16['F' - 0x20], 8) == 0);
    CHECK(display.buffer[128] == 0x00);

    CHECK(ssd1306_clear(&display) == ESP_OK);
    CHECK(ssd1306_draw_text(&display, 1, 0, "\x01") == ESP_OK);
    CHECK(memcmp(&display.buffer[2 * SSD1306_WIDTH], ssd1306_font8x16['?' - 0x20], 8) == 0);
    return 0;
}

static int draw_text_rejects_bad_arguments(void)
{
    ssd1306_t display = {0};
    CHECK(ssd1306_draw_text(NULL, 0, 0, "A") == ESP_ERR_INVALID_ARG);
    CHECK(ssd1306_draw_text(&display, 0, 0, NULL) == ESP_ERR_INVALID_ARG);
    CHECK(ssd1306_draw_text(&display, -1, 0, "A") == ESP_ERR_INVALID_ARG);
    CHECK(ssd1306_draw_text(&display, SSD1306_TEXT_LINES, 0, "A") == ESP_ERR_INVALID_ARG);
    CHECK(ssd1306_draw_text(&display, 0, -1, "A") == ESP_ERR_INVALID_ARG);
    CHECK(ssd1306_draw_text(&display, 0, SSD1306_TEXT_COLUMNS, "A") == ESP_ERR_INVALID_ARG);
    return 0;
}

static int flush_requires_initialized_display(void)
{
    ssd1306_t display = {0};
    CHECK(ssd1306_flush(&display) == ESP_ERR_INVALID_STATE);
    CHECK(ssd1306_flush(NULL) == ESP_ERR_INVALID_ARG);
    CHECK(tx_calls == 0);
    return 0;
}

static int flush_stops_at_first_failing_page(void)
{
    ssd1306_t display = {0};
    CHECK(ssd1306_init(&display, &bus, SSD1306_ADDRESS_DEFAULT) == ESP_OK);
    /* 初始化用了 17 次传输，第 3 页的数据传输失败。 */
    fail_tx_on = 17 + 3 * 2;
    CHECK(ssd1306_flush(&display) == ESP_ERR_TIMEOUT);
    CHECK(tx_calls == 17 + 3 * 2);
    return 0;
}

static int deinit_removes_device_and_is_idempotent(void)
{
    ssd1306_t display = {0};
    CHECK(ssd1306_deinit(&display) == ESP_OK);
    CHECK(rm_calls == 0);

    CHECK(ssd1306_init(&display, &bus, SSD1306_ADDRESS_DEFAULT) == ESP_OK);
    CHECK(ssd1306_deinit(&display) == ESP_OK);
    CHECK(rm_calls == 1);
    CHECK(!display.initialized && display.i2c_dev == NULL);
    CHECK(ssd1306_deinit(&display) == ESP_OK && rm_calls == 1);

    rm_result = ESP_FAIL;
    CHECK(ssd1306_init(&display, &bus, SSD1306_ADDRESS_DEFAULT) == ESP_OK);
    CHECK(ssd1306_deinit(&display) == ESP_FAIL);
    CHECK(display.initialized && display.i2c_dev == &device);
    return 0;
}

struct test_case { const char *name; int (*run)(void); };
#define TEST(name) {#name, name}
static const struct test_case cases[] = {
    TEST(init_configures_device_and_command_sequence),
    TEST(init_flushes_blank_screen_by_page),
    TEST(init_rejects_invalid_arguments),
    TEST(init_add_failure_keeps_object_clean),
    TEST(init_command_failure_releases_device),
    TEST(init_retained_handle_is_cleaned_before_retry),
    TEST(clear_zeroes_the_framebuffer),
    TEST(draw_text_places_glyph_bytes),
    TEST(draw_text_clips_and_replaces_control_characters),
    TEST(draw_text_rejects_bad_arguments),
    TEST(flush_requires_initialized_display),
    TEST(flush_stops_at_first_failing_page),
    TEST(deinit_removes_device_and_is_idempotent),
};

int ssd1306_test_count(void) { return (int)(sizeof(cases) / sizeof(cases[0])); }
const char *ssd1306_test_name(int index) { return cases[index].name; }
int ssd1306_test_run(int index)
{
    reset_mock();
    int line = cases[index].run();
    return line ? line : mock_fault;
}
