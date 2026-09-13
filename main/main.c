#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "servo.h"

#define SERVO_GPIO          GPIO_NUM_18
#define SERVO_CENTER_ANGLE  90
#define SERVO_RETRACT_ANGLE 45
#define SERVO_EXTEND_ANGLE  135
#define SERVO_STEP_DELAY_MS 20
#define SERVO_HOLD_MS       2000

static const char *TAG = "servo_demo";

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
    if (xTaskCreate(servo_test_task, "servo_test", 3072, NULL, 5, NULL) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create servo test task");
    }
}
