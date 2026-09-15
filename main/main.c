#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "servo.h"
#include "rain_sensor.h"

#define SERVO_GPIO          GPIO_NUM_18
#define SERVO_CENTER_ANGLE  90
#define SERVO_RETRACT_ANGLE 45
#define SERVO_EXTEND_ANGLE  135
#define SERVO_STEP_DELAY_MS 20
#define SERVO_HOLD_MS       2000

/* 默认接线：雨滴模块 DO -> GPIO27，低电平表示有雨。
 * 请按实际接线和模块输出电平修改这两个宏。 */
#define RAIN_SENSOR_GPIO           GPIO_NUM_27
#define RAIN_SENSOR_ACTIVE_LEVEL   0
#define RAIN_SAMPLE_INTERVAL_MS    100
#define RAIN_CONFIRM_SAMPLES       3

static const char *TAG = "servo_demo";
static const char *RAIN_TAG = "rain_sensor";

static void rain_sensor_task(void *arg)
{
    (void)arg;
    esp_err_t err = rain_sensor_init(RAIN_SENSOR_GPIO, RAIN_SENSOR_ACTIVE_LEVEL);
    if (err != ESP_OK) {
        ESP_LOGE(RAIN_TAG, "Init failed: %s", esp_err_to_name(err));
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(RAIN_TAG, "DO on GPIO%d, rain active level = %d",
             RAIN_SENSOR_GPIO, RAIN_SENSOR_ACTIVE_LEVEL);

    bool candidate = false;
    bool stable_state = false;
    bool state_valid = false;
    unsigned int same_count = 0;
    while (1) {
        bool is_raining;
        err = rain_sensor_read(&is_raining);
        if (err != ESP_OK) {
            ESP_LOGE(RAIN_TAG, "Read failed: %s", esp_err_to_name(err));
            same_count = 0;
        } else {
            /* 连续 3 次采样一致才确认，过滤阈值附近的短暂跳变。
             * 首次确认也输出日志，之后仅在状态变化时输出。 */
            if (same_count == 0 || is_raining != candidate) {
                candidate = is_raining;
                same_count = 1;
            } else if (same_count < RAIN_CONFIRM_SAMPLES) {
                ++same_count;
            }
            if (same_count >= RAIN_CONFIRM_SAMPLES &&
                (!state_valid || stable_state != candidate)) {
                stable_state = candidate;
                state_valid = true;
                ESP_LOGI(RAIN_TAG, "%s", stable_state ? "Rain detected" : "No rain");
            }
        }
        vTaskDelay(pdMS_TO_TICKS(RAIN_SAMPLE_INTERVAL_MS));
    }
}

/* 每 20ms 改变 1 度目标值，任务延时会让出 CPU。
 * 此变量记录最后下发角度；SG90 没有实际角度反馈。 */
static void move_slowly(int *current_angle, int target_angle)
{
    ESP_LOGI(TAG, "Commanded angle: %d -> %d", *current_angle, target_angle);
    while (*current_angle != target_angle) {
        *current_angle += (*current_angle < target_angle) ? 1 : -1;
        ESP_ERROR_CHECK(servo_set_angle((float)*current_angle));
        vTaskDelay(pdMS_TO_TICKS(SERVO_STEP_DELAY_MS));
    }
}

static void servo_test_task(void *arg)
{
    (void)arg;
    ESP_ERROR_CHECK(servo_init(SERVO_GPIO));
    int current_angle = SERVO_CENTER_ANGLE;
    ESP_LOGI(TAG, "SG90 on GPIO%d, 50Hz, center command = 90", SERVO_GPIO);
    vTaskDelay(pdMS_TO_TICKS(SERVO_HOLD_MS));

    while (1) {
        ESP_LOGI(TAG, "Retract simulation");
        move_slowly(&current_angle, SERVO_RETRACT_ANGLE);
        vTaskDelay(pdMS_TO_TICKS(SERVO_HOLD_MS));

        ESP_LOGI(TAG, "Extend simulation");
        move_slowly(&current_angle, SERVO_EXTEND_ANGLE);
        vTaskDelay(pdMS_TO_TICKS(SERVO_HOLD_MS));
    }
}

void app_main(void)
{
    // ESP-IDF 已启动 FreeRTOS，不需要再调用 vTaskStartScheduler。
    if (xTaskCreate(rain_sensor_task, "rain_sensor", 3072, NULL, 5, NULL) != pdPASS) {
        ESP_LOGE(RAIN_TAG, "Failed to create rain sensor task");
    }
    // 舵机仍运行原来的往返测试；雨滴任务目前只检测并输出状态。
    if (xTaskCreate(servo_test_task, "servo_test", 3072, NULL, 5, NULL) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create servo test task");
    }
}
