#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "servo.h"
#include "rain_sensor.h"
#include "key.h"
#include "environment.h"
#include "clothes_control.h"

#define SERVO_GPIO          GPIO_NUM_18
#define SERVO_CENTER_ANGLE  90
#define SERVO_RETRACT_ANGLE 45
#define SERVO_EXTEND_ANGLE  135
#define SERVO_STEP_DELAY_MS 20
#define KEY_EXTEND_GPIO     GPIO_NUM_25
#define KEY_RETRACT_GPIO    GPIO_NUM_26
#define CONTROL_POLL_MS     10
#define RAIN_SENSOR_GPIO         GPIO_NUM_27
#define RAIN_SENSOR_ACTIVE_LEVEL 0
#define RAIN_SAMPLE_INTERVAL_US  100000
#define RAIN_CONFIRM_SAMPLES     3

static const char *TAG = "clothes_control";

typedef struct {
    bool candidate;
    bool raining;
    bool valid;
    unsigned int same_count;
} rain_filter_t;

static void sample_rain(rain_filter_t *filter)
{
    bool raining;
    esp_err_t err = rain_sensor_read(&raining);
    if (err != ESP_OK) {
        filter->same_count = 0;
        // 读取失败保留最后确认状态，不把错误当作雨停。
        ESP_LOGW(TAG, "Rain read failed: %s", esp_err_to_name(err));
        return;
    }
    if (filter->same_count == 0 || raining != filter->candidate) {
        filter->candidate = raining;
        filter->same_count = 1;
    } else if (filter->same_count < RAIN_CONFIRM_SAMPLES) {
        ++filter->same_count;
    }
    if (filter->same_count >= RAIN_CONFIRM_SAMPLES &&
        (!filter->valid || filter->raining != filter->candidate)) {
        filter->raining = filter->candidate;
        filter->valid = true;
        ESP_LOGI(TAG, "%s", filter->raining ? "Rain detected: retract" : "No rain");
    }
}

/* 只有本任务操作舵机并维护位置，按键/雨滴/18点定时共用同一策略去重。
 * 位置是最后成功下发的角度状态，SG90 不提供实际位置反馈。 */
static void clothes_control_task(void *arg)
{
    (void)arg;
    key_pair_t keys = {0};
    esp_err_t err = key_init(&keys, KEY_EXTEND_GPIO, KEY_RETRACT_GPIO);
    const bool keys_enabled = err == ESP_OK;
    if (!keys_enabled) ESP_LOGE(TAG, "Key init failed: %s", esp_err_to_name(err));
    err = rain_sensor_init(RAIN_SENSOR_GPIO, RAIN_SENSOR_ACTIVE_LEVEL);
    const bool rain_enabled = err == ESP_OK;
    if (!rain_enabled) ESP_LOGE(TAG, "Rain init failed: %s", esp_err_to_name(err));
    err = servo_init(SERVO_GPIO);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Servo init failed: %s", esp_err_to_name(err));
        vTaskDelete(NULL);
        return;
    }

    clothes_control_t control;
    clothes_control_init(&control);
    rain_filter_t rain = {0};
    int current_angle = SERVO_CENTER_ANGLE;
    int target_angle = SERVO_CENTER_ANGLE;
    int64_t last_step_us = esp_timer_get_time();
    int64_t next_rain_us = 0;
    ESP_LOGI(TAG, "Keys: extend GPIO%d, retract GPIO%d; rain GPIO%d; daily retract at 18:00",
             KEY_EXTEND_GPIO, KEY_RETRACT_GPIO, RAIN_SENSOR_GPIO);

    while (1) {
        const int64_t now = esp_timer_get_time();
        clothes_inputs_t inputs = {0};
        if (keys_enabled) {
            key_event_t event;
            err = key_poll(&keys, &event);
            if (err == ESP_OK) {
                if (event == KEY_EVENT_RETRACT) inputs.manual_command = CLOTHES_CMD_RETRACT;
                if (event == KEY_EVENT_EXTEND) inputs.manual_command = CLOTHES_CMD_EXTEND;
            } else {
                ESP_LOGE(TAG, "Key read failed: %s", esp_err_to_name(err));
            }
        }
        if (rain_enabled && now >= next_rain_us) {
            sample_rain(&rain);
            next_rain_us = now + RAIN_SAMPLE_INTERVAL_US;
        }
        inputs.rain_valid = rain.valid;
        inputs.raining = rain.raining;

        rtc_datetime_t time;
        inputs.time_valid = environment_get_time(&time);
        if (inputs.time_valid) {
            inputs.date_key = (uint32_t)time.year * 10000U + (uint32_t)time.month * 100U + time.day;
            inputs.seconds_of_day = (uint32_t)time.hour * 3600U + (uint32_t)time.minute * 60U + time.second;
        }
        const uint32_t previous_evening_date = control.last_evening_date;
        const clothes_command_t command = clothes_control_update(&control, &inputs);
        if (control.last_evening_date != previous_evening_date) {
            ESP_LOGI(TAG, "18:00 check for %lu: %s", (unsigned long)control.last_evening_date,
                     command == CLOTHES_CMD_RETRACT ? "retract" : "already retracted/retracting, skip");
        }
        if (command != CLOTHES_CMD_NONE) {
            target_angle = command == CLOTHES_CMD_RETRACT ? SERVO_RETRACT_ANGLE : SERVO_EXTEND_ANGLE;
            ESP_LOGI(TAG, "%s: %d -> %d deg", command == CLOTHES_CMD_RETRACT ? "Retract" : "Extend",
                     current_angle, target_angle);
        }

        if (current_angle != target_angle && now - last_step_us >= SERVO_STEP_DELAY_MS * 1000) {
            const int next_angle = current_angle + (current_angle < target_angle ? 1 : -1);
            err = servo_set_angle((float)next_angle);
            if (err == ESP_OK) {
                current_angle = next_angle;
            } else {
                target_angle = current_angle;
                clothes_control_failed(&control);
                ESP_LOGE(TAG, "Servo failed: %s; position unknown, waiting for next command",
                         esp_err_to_name(err));
            }
            last_step_us = now;
        }
        if (control.active_command != CLOTHES_CMD_NONE && current_angle == target_angle) {
            const clothes_command_t completed = control.active_command;
            clothes_control_complete(&control, completed);
            ESP_LOGI(TAG, "%s command complete", completed == CLOTHES_CMD_RETRACT ? "Retract" : "Extend");
        }
        vTaskDelay(pdMS_TO_TICKS(CONTROL_POLL_MS));
    }
}

void app_main(void)
{
    esp_err_t err = environment_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Environment service failed: %s; timed retract disabled", esp_err_to_name(err));
    }
    if (xTaskCreate(clothes_control_task, "clothes_control", 4096, NULL, 5, NULL) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create clothes control task");
    }
}
