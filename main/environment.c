#include "environment.h"

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_console.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "sht30.h"

/* DS3231(0x68) 与 SHT30(0x44) 共用这条总线，不可分别创建 I2C0。 */
#define SENSOR_SDA_GPIO GPIO_NUM_21
#define SENSOR_SCL_GPIO GPIO_NUM_22
#define SHT30_ADDRESS SHT30_I2C_ADDRESS_DEFAULT
#define RTC_POLL_US 1000000
#define SHT30_POLL_US 2000000
#define RTC_MAX_AGE_US 3000000

typedef struct {
    rtc_datetime_t time;
    int64_t sampled_at_us;
    bool valid;
} clock_sample_t;

static QueueHandle_t s_clock_queue;
static QueueHandle_t s_set_time_queue;
static const char *TAG = "environment";

bool environment_get_time(rtc_datetime_t *time)
{
    clock_sample_t sample;
    if (time == NULL || s_clock_queue == NULL ||
        xQueuePeek(s_clock_queue, &sample, 0) != pdTRUE || !sample.valid ||
        esp_timer_get_time() - sample.sampled_at_us > RTC_MAX_AGE_US) {
        return false;
    }
    *time = sample.time;
    return true;
}

static esp_err_t ensure_rtc(rtc_t *rtc, i2c_master_bus_handle_t bus, bool *ready)
{
    if (*ready) {
        return ESP_OK;
    }
    if (rtc->i2c_dev != NULL) {
        esp_err_t err = rtc_ds3231_deinit(rtc);
        if (err != ESP_OK) {
            return err;
        }
    }
    esp_err_t err = rtc_ds3231_init(rtc, bus);
    *ready = err == ESP_OK;
    return err;
}

static void sample_sht30(sht30_t *sensor, i2c_master_bus_handle_t bus)
{
    esp_err_t err;
    if (!sensor->initialized) {
        if (sensor->i2c_dev != NULL) {
            err = sht30_deinit(sensor);
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "SHT30 cleanup failed: %s", esp_err_to_name(err));
                return;
            }
        }
        err = sht30_init(sensor, bus, SHT30_ADDRESS);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "SHT30 init failed: %s", esp_err_to_name(err));
            return;
        }
    }
    float temperature_c;
    float humidity_percent;
    err = sht30_read(sensor, &temperature_c, &humidity_percent);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Temperature: %.2f C, Humidity: %.2f %%RH",
                 temperature_c, humidity_percent);
    } else {
        ESP_LOGW(TAG, "SHT30 read failed: %s", esp_err_to_name(err));
    }
}

static void environment_task(void *arg)
{
    (void)arg;
    const i2c_master_bus_config_t config = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = SENSOR_SDA_GPIO,
        .scl_io_num = SENSOR_SCL_GPIO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    i2c_master_bus_handle_t bus = NULL;
    while (bus == NULL) {
        esp_err_t err = i2c_new_master_bus(&config, &bus);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "I2C init failed: %s; retry in 2s", esp_err_to_name(err));
            vTaskDelay(pdMS_TO_TICKS(2000));
        }
    }
    ESP_LOGI(TAG, "SDA GPIO%d, SCL GPIO%d; SHT30 0x%02X, DS3231 0x68",
             SENSOR_SDA_GPIO, SENSOR_SCL_GPIO, SHT30_ADDRESS);

    rtc_t rtc = {0};
    sht30_t sht30 = {0};
    bool rtc_ready = false;
    esp_err_t last_rtc_error = ESP_ERR_NOT_FOUND;
    int64_t next_rtc_us = 0;
    int64_t next_sht30_us = 0;
    while (1) {
        rtc_datetime_t requested_time;
        if (xQueueReceive(s_set_time_queue, &requested_time, 0) == pdTRUE) {
            // 校时前取消旧快照，再读回芯片，避免写入期间继续使用旧值。
            const clock_sample_t invalid_sample = {0};
            xQueueOverwrite(s_clock_queue, &invalid_sample);
            esp_err_t err = ensure_rtc(&rtc, bus, &rtc_ready);
            if (err == ESP_OK) {
                err = rtc_ds3231_set_datetime(&rtc, &requested_time);
            }
            if (err == ESP_OK) {
                ESP_LOGI(TAG, "RTC set: %04u-%02u-%02u %02u:%02u:%02u (local)",
                         requested_time.year, requested_time.month, requested_time.day,
                         requested_time.hour, requested_time.minute, requested_time.second);
            } else {
                ESP_LOGE(TAG, "RTC set failed: %s", esp_err_to_name(err));
            }
            next_rtc_us = 0;
        }

        if (esp_timer_get_time() >= next_rtc_us) {
            clock_sample_t sample = {0};
            esp_err_t err = ensure_rtc(&rtc, bus, &rtc_ready);
            if (err == ESP_OK) {
                err = rtc_ds3231_get_datetime(&rtc, &sample.time);
            }
            sample.sampled_at_us = esp_timer_get_time();
            sample.valid = err == ESP_OK;
            xQueueOverwrite(s_clock_queue, &sample);
            if (err != last_rtc_error) {
                if (err == ESP_OK) {
                    ESP_LOGI(TAG, "RTC valid; daily retract at 18:00 local time");
                } else {
                    ESP_LOGW(TAG, "RTC unavailable/invalid: %s; timed retract disabled. "
                             "Use rtc_set YYYY-MM-DD HH:MM:SS to set the clock.", esp_err_to_name(err));
                }
                last_rtc_error = err;
            }
            next_rtc_us = sample.sampled_at_us + RTC_POLL_US;
        }
        if (esp_timer_get_time() >= next_sht30_us) {
            sample_sht30(&sht30, bus);
            next_sht30_us = esp_timer_get_time() + SHT30_POLL_US;
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

static int cmd_rtc_get(int argc, char **argv)
{
    (void)argv;
    if (argc != 1) {
        printf("Usage: rtc_get\n");
        return 1;
    }
    rtc_datetime_t time;
    if (!environment_get_time(&time)) {
        printf("RTC time unavailable. Check DS3231 wiring and use rtc_set to set local time.\n");
        return 1;
    }
    printf("%04u-%02u-%02u %02u:%02u:%02u (local time)\n",
           time.year, time.month, time.day, time.hour, time.minute, time.second);
    return 0;
}

static int cmd_rtc_set(int argc, char **argv)
{
    unsigned int year, month, day, hour, minute, second;
    char extra;
    if (argc != 3 || sscanf(argv[1], "%4u-%2u-%2u%c", &year, &month, &day, &extra) != 3 ||
        sscanf(argv[2], "%2u:%2u:%2u%c", &hour, &minute, &second, &extra) != 3 ||
        year < 2000 || year > 2099 || month < 1 || month > 12 || day < 1 || day > 31 ||
        hour > 23 || minute > 59 || second > 59) {
        printf("Usage: rtc_set YYYY-MM-DD HH:MM:SS (local time, year 2000..2099)\n");
        return 1;
    }
    const rtc_datetime_t time = {
        .year = year, .month = month, .day = day,
        .hour = hour, .minute = minute, .second = second,
    };
    if (s_set_time_queue == NULL || xQueueSend(s_set_time_queue, &time, 0) != pdTRUE) {
        printf("RTC service busy/unavailable; try again.\n");
        return 1;
    }
    printf("RTC set request queued; see the RTC set/failed log for the result.\n");
    return 0;
}

static esp_err_t start_clock_console(void)
{
    esp_console_repl_t *repl = NULL;
    esp_console_repl_config_t repl_config = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    const esp_console_dev_uart_config_t uart_config = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
    repl_config.prompt = "drying>";
    repl_config.max_cmdline_length = 96;
    esp_err_t err = esp_console_new_repl_uart(&uart_config, &repl_config, &repl);
    if (err != ESP_OK) {
        return err;
    }
    const esp_console_cmd_t get_command = {
        .command = "rtc_get", .help = "Show the latest valid local RTC time", .func = cmd_rtc_get,
    };
    const esp_console_cmd_t set_command = {
        .command = "rtc_set", .help = "Set DS3231 local time: rtc_set YYYY-MM-DD HH:MM:SS",
        .func = cmd_rtc_set,
    };
    const esp_err_t get_error = esp_console_cmd_register(&get_command);
    const esp_err_t set_error = esp_console_cmd_register(&set_command);
    if (get_error != ESP_OK) {
        ESP_LOGE(TAG, "rtc_get registration failed: %s", esp_err_to_name(get_error));
    }
    if (set_error != ESP_OK) {
        ESP_LOGE(TAG, "rtc_set registration failed: %s", esp_err_to_name(set_error));
    }
    /* new_repl 已注册 help 并创建任务。即使命令注册失败也启动 REPL，
     * 让已注册命令可用；IDF 5.4 的 del 不会终止等通知的任务，不能在此释放。 */
    err = esp_console_start_repl(repl);
    if (err != ESP_OK) {
        return err;
    }
    return get_error != ESP_OK ? get_error : set_error;
}

esp_err_t environment_start(void)
{
    s_clock_queue = xQueueCreate(1, sizeof(clock_sample_t));
    s_set_time_queue = xQueueCreate(1, sizeof(rtc_datetime_t));
    if (s_clock_queue == NULL || s_set_time_queue == NULL ||
        xTaskCreate(environment_task, "environment", 4096, NULL, 5, NULL) != pdPASS) {
        if (s_clock_queue != NULL) vQueueDelete(s_clock_queue);
        if (s_set_time_queue != NULL) vQueueDelete(s_set_time_queue);
        s_clock_queue = NULL;
        s_set_time_queue = NULL;
        return ESP_ERR_NO_MEM;
    }
    esp_err_t err = start_clock_console();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "RTC console failed: %s; sensor task remains active", esp_err_to_name(err));
    }
    return ESP_OK;
}
