/* Include the actual service to exercise its static console handlers and startup. */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
static int mock_printf(const char *format, ...);
#define printf mock_printf
#include "../main/environment.c"
#undef printf

#define CHECK(condition) do { if (!(condition)) return __LINE__; } while (0)
#define COUNT(array) (sizeof(array) / sizeof((array)[0]))
struct mock_queue { unsigned size; bool full, deleted; unsigned char data[128]; };
static struct mock_queue queues[2];
static int create_calls, delete_calls, task_calls, fail_create, fault;
static BaseType_t task_result;
static int64_t now_us;
static int new_calls, register_calls, help_calls, start_calls, del_calls;
static esp_err_t new_result, get_result, set_result, start_result;
static esp_console_cmd_t commands[2];
static char printed[512];

static esp_err_t mock_repl_del(esp_console_repl_t *repl)
{
    (void)repl;
    ++del_calls;
    return ESP_OK;
}
static esp_console_repl_t repl_object = {.del = mock_repl_del};

static void reset_mock(void)
{
    memset(queues, 0, sizeof(queues));
    memset(commands, 0, sizeof(commands));
    printed[0] = '\0';
    create_calls = delete_calls = task_calls = fail_create = fault = 0;
    new_calls = register_calls = help_calls = start_calls = del_calls = 0;
    new_result = get_result = set_result = start_result = ESP_OK;
    task_result = pdPASS;
    now_us = 1000000;
    s_clock_queue = s_set_time_queue = NULL;
}

static int mock_printf(const char *format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    int result = vsnprintf(printed, sizeof(printed), format, arguments);
    va_end(arguments);
    return result;
}
void mock_log(const char *tag, const char *format, ...) { (void)tag; (void)format; }
const char *esp_err_to_name(esp_err_t error) { (void)error; return "mock_error"; }
int64_t esp_timer_get_time(void) { return now_us; }

QueueHandle_t xQueueCreate(UBaseType_t length, UBaseType_t size)
{
    ++create_calls;
    if (length != 1 || size > sizeof(queues[0].data) || create_calls > 2) {
        fault = __LINE__;
        return NULL;
    }
    if (create_calls == fail_create) return NULL;
    queues[create_calls - 1].size = size;
    return &queues[create_calls - 1];
}
BaseType_t xQueuePeek(QueueHandle_t queue, void *output, TickType_t ticks)
{
    if (queue == NULL || queue->deleted || ticks != 0) {
        fault = __LINE__;
        return pdFALSE;
    }
    if (!queue->full) return pdFALSE;
    memcpy(output, queue->data, queue->size);
    return pdTRUE;
}
BaseType_t xQueueOverwrite(QueueHandle_t queue, const void *input)
{
    if (queue == NULL || queue->deleted) { fault = __LINE__; return pdFALSE; }
    memcpy(queue->data, input, queue->size);
    queue->full = true;
    return pdTRUE;
}
BaseType_t xQueueSend(QueueHandle_t queue, const void *input, TickType_t ticks)
{
    if (queue == NULL || queue->deleted || ticks != 0) { fault = __LINE__; return pdFALSE; }
    if (queue->full) return pdFALSE;
    return xQueueOverwrite(queue, input);
}
BaseType_t xQueueReceive(QueueHandle_t queue, void *output, TickType_t ticks)
{
    BaseType_t result = xQueuePeek(queue, output, ticks);
    if (result == pdTRUE) queue->full = false;
    return result;
}
void vQueueDelete(QueueHandle_t queue)
{
    ++delete_calls;
    if (queue == NULL || queue->deleted) { fault = __LINE__; return; }
    queue->deleted = true;
}
BaseType_t xTaskCreate(TaskFunction_t task, const char *name, uint32_t stack,
                      void *argument, UBaseType_t priority, void *handle)
{
    ++task_calls;
    if (task != environment_task || strcmp(name, "environment") != 0 || stack != 4096 ||
        argument != NULL || priority != 5 || handle != NULL) fault = __LINE__;
    return task_result; /* Do not execute the infinite sensor task. */
}
void vTaskDelay(TickType_t ticks) { (void)ticks; fault = __LINE__; }

esp_err_t esp_console_new_repl_uart(const esp_console_dev_uart_config_t *uart,
                                  const esp_console_repl_config_t *config,
                                  esp_console_repl_t **repl)
{
    ++new_calls;
    if (uart == NULL || config == NULL || repl == NULL ||
        strcmp(config->prompt, "drying>") != 0 || config->max_cmdline_length != 96) {
        fault = __LINE__;
        return ESP_FAIL;
    }
    if (new_result == ESP_OK) *repl = &repl_object;
    return new_result;
}
esp_err_t esp_console_cmd_register(const esp_console_cmd_t *command)
{
    ++register_calls;
    if (register_calls > 2 || command == NULL || command->func == NULL || command->help == NULL) {
        fault = __LINE__;
        return ESP_FAIL;
    }
    commands[register_calls - 1] = *command;
    if (strcmp(command->command, "rtc_get") == 0) return get_result;
    if (strcmp(command->command, "rtc_set") == 0) return set_result;
    fault = __LINE__;
    return ESP_FAIL;
}
esp_err_t esp_console_register_help_command(void) { ++help_calls; return ESP_OK; }
esp_err_t esp_console_start_repl(esp_console_repl_t *repl)
{
    ++start_calls;
    if (repl != &repl_object || register_calls != 2) fault = __LINE__;
    return start_result;
}

/* Hardware belongs to the unexecuted task; unexpected access must fail tests. */
esp_err_t i2c_new_master_bus(const i2c_master_bus_config_t *config, i2c_master_bus_handle_t *bus)
{ (void)config; (void)bus; fault = __LINE__; return ESP_FAIL; }
esp_err_t rtc_ds3231_init(rtc_t *rtc, i2c_master_bus_handle_t bus)
{ (void)rtc; (void)bus; fault = __LINE__; return ESP_FAIL; }
esp_err_t rtc_ds3231_deinit(rtc_t *rtc)
{ (void)rtc; fault = __LINE__; return ESP_FAIL; }
esp_err_t rtc_ds3231_get_datetime(rtc_t *rtc, rtc_datetime_t *time)
{ (void)rtc; (void)time; fault = __LINE__; return ESP_FAIL; }
esp_err_t rtc_ds3231_set_datetime(rtc_t *rtc, const rtc_datetime_t *time)
{ (void)rtc; (void)time; fault = __LINE__; return ESP_FAIL; }
esp_err_t sht30_init(sht30_t *sensor, i2c_master_bus_handle_t bus, uint8_t address)
{ (void)sensor; (void)bus; (void)address; fault = __LINE__; return ESP_FAIL; }
esp_err_t sht30_deinit(sht30_t *sensor)
{ (void)sensor; fault = __LINE__; return ESP_FAIL; }
esp_err_t sht30_read(sht30_t *sensor, float *temperature, float *humidity)
{ (void)sensor; (void)temperature; (void)humidity; fault = __LINE__; return ESP_FAIL; }

static void populate_clock(bool valid)
{
    s_clock_queue = xQueueCreate(1, sizeof(clock_sample_t));
    const clock_sample_t sample = {.time = {2026, 9, 23, 18, 7, 59},
                                   .sampled_at_us = now_us, .valid = valid};
    xQueueOverwrite(s_clock_queue, &sample);
}
static int fresh_clock_and_exact_age_boundary(void)
{
    populate_clock(true);
    rtc_datetime_t time = {0};
    CHECK(environment_get_time(&time));
    CHECK(time.year == 2026 && time.month == 9 && time.day == 23);
    CHECK(time.hour == 18 && time.minute == 7 && time.second == 59);
    now_us += 3000000;
    CHECK(environment_get_time(&time));
    return 0;
}
static int expired_clock_preserves_output(void)
{
    populate_clock(true);
    rtc_datetime_t output, original;
    memset(&output, 0xA5, sizeof(output));
    memcpy(&original, &output, sizeof(output));
    now_us += 3000001;
    CHECK(!environment_get_time(&output));
    CHECK(memcmp(&original, &output, sizeof(output)) == 0);
    return 0;
}
static int missing_empty_invalid_clock(void)
{
    rtc_datetime_t output = {2042, 1, 1, 0, 0, 0};
    CHECK(!environment_get_time(NULL));
    CHECK(!environment_get_time(&output) && output.year == 2042);
    populate_clock(false);
    CHECK(!environment_get_time(&output) && output.year == 2042);
    s_clock_queue->full = false;
    CHECK(!environment_get_time(&output) && output.year == 2042);
    return 0;
}
static int rtc_get_command(void)
{
    CHECK(cmd_rtc_get(2, NULL) == 1 && strstr(printed, "Usage") != NULL);
    CHECK(cmd_rtc_get(1, NULL) == 1 && strstr(printed, "unavailable") != NULL);
    populate_clock(true);
    CHECK(cmd_rtc_get(1, NULL) == 0);
    CHECK(strcmp(printed, "2026-09-23 18:07:59 (local time)\n") == 0);
    now_us += 3000001;
    CHECK(cmd_rtc_get(1, NULL) == 1);
    return 0;
}
static int rtc_set_queues_local_time(void)
{
    s_set_time_queue = xQueueCreate(1, sizeof(rtc_datetime_t));
    char *arguments[] = {"rtc_set", "2026-09-23", "18:07:59"};
    CHECK(cmd_rtc_set(3, arguments) == 0 && s_set_time_queue->full);
    rtc_datetime_t time;
    CHECK(xQueuePeek(s_set_time_queue, &time, 0) == pdTRUE);
    CHECK(time.year == 2026 && time.month == 9 && time.day == 23);
    CHECK(time.hour == 18 && time.minute == 7 && time.second == 59);
    CHECK(strstr(printed, "queued") != NULL);
    return 0;
}
static int rtc_set_rejects_bad_input_and_overflow(void)
{
    s_set_time_queue = xQueueCreate(1, sizeof(rtc_datetime_t));
    const char *dates[] = {"1999-01-01", "2100-01-01", "2026-00-01", "2026-13-01",
        "2026-09-00", "2026-09-32", "2026-09-23x", "2026/09/23", "4294969322-09-23",
        "2026-4294967305-23", "2026-09-4294967319"};
    const char *times[] = {"24:00:00", "18:60:00", "18:00:60", "18:07:59x",
        "18-07-59", "4294967314:07:59", "18:4294967303:59", "18:07:4294967355"};
    char *arguments[] = {"rtc_set", "2026-09-23", "18:07:59"};
    CHECK(cmd_rtc_set(2, arguments) == 1);
    CHECK(cmd_rtc_set(4, arguments) == 1);
    for (size_t i = 0; i < COUNT(dates); ++i) {
        arguments[1] = (char *)dates[i];
        CHECK(cmd_rtc_set(3, arguments) == 1 && !s_set_time_queue->full);
    }
    arguments[1] = "2026-09-23";
    for (size_t i = 0; i < COUNT(times); ++i) {
        arguments[2] = (char *)times[i];
        CHECK(cmd_rtc_set(3, arguments) == 1 && !s_set_time_queue->full);
    }
    return 0;
}
static int rtc_set_busy_or_missing_service(void)
{
    char *arguments[] = {"rtc_set", "2026-09-23", "18:07:59"};
    CHECK(cmd_rtc_set(3, arguments) == 1 && strstr(printed, "busy/unavailable") != NULL);
    s_set_time_queue = xQueueCreate(1, sizeof(rtc_datetime_t));
    CHECK(cmd_rtc_set(3, arguments) == 0);
    arguments[2] = "19:00:00";
    CHECK(cmd_rtc_set(3, arguments) == 1);
    rtc_datetime_t time;
    CHECK(xQueuePeek(s_set_time_queue, &time, 0) == pdTRUE && time.hour == 18);
    return 0;
}
static int console_registers_only_rtc_commands(void)
{
    CHECK(start_clock_console() == ESP_OK);
    CHECK(new_calls == 1 && register_calls == 2 && start_calls == 1);
    CHECK(commands[0].func == cmd_rtc_get && commands[1].func == cmd_rtc_set);
    CHECK(help_calls == 0 && del_calls == 0);
    return 0;
}
static int console_creation_failure(void)
{
    new_result = ESP_ERR_NO_MEM;
    CHECK(start_clock_console() == ESP_ERR_NO_MEM);
    CHECK(register_calls == 0 && start_calls == 0 && del_calls == 0 && help_calls == 0);
    return 0;
}
static int console_get_registration_failure_still_starts(void)
{
    get_result = ESP_ERR_NO_MEM;
    CHECK(start_clock_console() == ESP_ERR_NO_MEM);
    CHECK(register_calls == 2 && start_calls == 1 && del_calls == 0 && help_calls == 0);
    return 0;
}
static int console_set_registration_failure_still_starts(void)
{
    set_result = ESP_FAIL;
    CHECK(start_clock_console() == ESP_FAIL);
    CHECK(register_calls == 2 && start_calls == 1 && del_calls == 0 && help_calls == 0);
    return 0;
}
static int console_start_failure_does_not_delete_live_repl(void)
{
    start_result = ESP_ERR_INVALID_STATE;
    CHECK(start_clock_console() == ESP_ERR_INVALID_STATE);
    CHECK(start_calls == 1 && del_calls == 0 && help_calls == 0);
    return 0;
}
static int service_starts_task_and_console(void)
{
    CHECK(environment_start() == ESP_OK);
    CHECK(create_calls == 2 && task_calls == 1 && start_calls == 1 && delete_calls == 0);
    CHECK(s_clock_queue->size == sizeof(clock_sample_t));
    CHECK(s_set_time_queue->size == sizeof(rtc_datetime_t));
    return 0;
}
static int service_queue_failure_cleans_up(void)
{
    for (int failure = 1; failure <= 2; ++failure) {
        reset_mock();
        fail_create = failure;
        CHECK(environment_start() == ESP_ERR_NO_MEM);
        CHECK(delete_calls == 1 && task_calls == 0 && new_calls == 0);
        CHECK(s_clock_queue == NULL && s_set_time_queue == NULL);
    }
    return 0;
}
static int service_task_failure_cleans_up(void)
{
    task_result = pdFALSE;
    CHECK(environment_start() == ESP_ERR_NO_MEM);
    CHECK(delete_calls == 2 && task_calls == 1 && new_calls == 0);
    CHECK(s_clock_queue == NULL && s_set_time_queue == NULL);
    return 0;
}
static int service_console_failure_keeps_sensor_task(void)
{
    new_result = ESP_FAIL;
    CHECK(environment_start() == ESP_OK);
    CHECK(task_calls == 1 && new_calls == 1 && delete_calls == 0);
    CHECK(s_clock_queue != NULL && s_set_time_queue != NULL);
    return 0;
}

#define TEST(function) {#function, function}
static const struct { const char *name; int (*run)(void); } tests[] = {
    TEST(fresh_clock_and_exact_age_boundary), TEST(expired_clock_preserves_output),
    TEST(missing_empty_invalid_clock), TEST(rtc_get_command), TEST(rtc_set_queues_local_time),
    TEST(rtc_set_rejects_bad_input_and_overflow), TEST(rtc_set_busy_or_missing_service),
    TEST(console_registers_only_rtc_commands), TEST(console_creation_failure),
    TEST(console_get_registration_failure_still_starts), TEST(console_set_registration_failure_still_starts),
    TEST(console_start_failure_does_not_delete_live_repl), TEST(service_starts_task_and_console),
    TEST(service_queue_failure_cleans_up), TEST(service_task_failure_cleans_up),
    TEST(service_console_failure_keeps_sensor_task),
};
int environment_test_count(void) { return (int)COUNT(tests); }
const char *environment_test_name(int index)
{ return index >= 0 && index < environment_test_count() ? tests[index].name : "invalid_index"; }
int environment_test_run(int index)
{
    if (index < 0 || index >= environment_test_count()) return __LINE__;
    reset_mock();
    int result = tests[index].run();
    return result ? result : fault;
}
