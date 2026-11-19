/* Test the pure JSON builder used by the local web page; no network needed. */
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "web_status.h"

#define CHECK(condition) do { if (!(condition)) return __LINE__; } while (0)

static web_status_t full_status(void)
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

static int renders_full_status_exactly(void)
{
    const web_status_t status = full_status();
    char buffer[512];
    const size_t length = web_status_json(&status, buffer, sizeof(buffer));
    const char *expected =
        "{\"time\":\"2026-05-01 12:34:56\",\"time_valid\":true,"
        "\"rain\":false,\"rain_valid\":true,"
        "\"temperature_c\":23.5,\"humidity_percent\":41.5,\"climate_valid\":true,"
        "\"lux\":1234.5,\"light_valid\":true,"
        "\"position\":\"retracted\",\"mode\":\"auto\","
        "\"wifi_connected\":true,\"ip\":\"192.168.1.10\"}";
    CHECK(length == strlen(expected));
    CHECK(strcmp(buffer, expected) == 0);
    return 0;
}

static int renders_invalid_sensor_flags(void)
{
    web_status_t status = {0};
    char buffer[512];
    CHECK(web_status_json(&status, buffer, sizeof(buffer)) > 0);
    CHECK(strstr(buffer, "\"time_valid\":false") != NULL);
    CHECK(strstr(buffer, "\"rain_valid\":false") != NULL);
    CHECK(strstr(buffer, "\"climate_valid\":false") != NULL);
    CHECK(strstr(buffer, "\"light_valid\":false") != NULL);
    CHECK(strstr(buffer, "\"position\":\"unknown\"") != NULL);
    CHECK(strstr(buffer, "\"mode\":\"auto\"") != NULL);
    CHECK(strstr(buffer, "\"wifi_connected\":false") != NULL);
    CHECK(strstr(buffer, "\"ip\":\"\"") != NULL);
    return 0;
}

static int renders_every_position_and_mode_name(void)
{
    static const struct { int position; const char *name; } positions[] = {
        { 0, "unknown" }, { 1, "extended" }, { 2, "retracted" },
        { 3, "moving_extend" }, { 4, "moving_retract" }, { 9, "unknown" },
    };
    char buffer[512];
    char expected[64];
    for (size_t i = 0; i < sizeof(positions) / sizeof(positions[0]); ++i) {
        web_status_t status = full_status();
        status.position = positions[i].position;
        CHECK(web_status_json(&status, buffer, sizeof(buffer)) > 0);
        snprintf(expected, sizeof(expected), "\"position\":\"%s\"", positions[i].name);
        CHECK(strstr(buffer, expected) != NULL);
    }

    web_status_t manual = full_status();
    manual.mode = 1;
    CHECK(web_status_json(&manual, buffer, sizeof(buffer)) > 0);
    CHECK(strstr(buffer, "\"mode\":\"manual\"") != NULL);
    return 0;
}

static int rejects_bad_arguments(void)
{
    web_status_t status = full_status();
    char buffer[64];
    CHECK(web_status_json(NULL, buffer, sizeof(buffer)) == 0);
    CHECK(web_status_json(&status, NULL, sizeof(buffer)) == 0);
    CHECK(web_status_json(&status, buffer, 0) == 0);
    return 0;
}

static int truncates_safely(void)
{
    const web_status_t status = full_status();
    char buffer[16];
    memset(buffer, 'x', sizeof(buffer));
    const size_t length = web_status_json(&status, buffer, sizeof(buffer));
    CHECK(length == sizeof(buffer) - 1);
    CHECK(buffer[sizeof(buffer) - 1] == '\0');
    CHECK(strlen(buffer) == sizeof(buffer) - 1);
    return 0;
}

struct test_case { const char *name; int (*run)(void); };
#define TEST(name) {#name, name}
static const struct test_case cases[] = {
    TEST(renders_full_status_exactly),
    TEST(renders_invalid_sensor_flags),
    TEST(renders_every_position_and_mode_name),
    TEST(rejects_bad_arguments),
    TEST(truncates_safely),
};

int web_status_test_count(void) { return (int)(sizeof(cases) / sizeof(cases[0])); }
const char *web_status_test_name(int index) { return cases[index].name; }
int web_status_test_run(int index) { return cases[index].run(); }
