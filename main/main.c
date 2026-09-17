#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "servo.h"
#include "rain_sensor.h"
#include "sht30.h"

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

/* SHT30 默认接线，可按实际接线修改；ADDR 拉高时地址改为 0x45。 */
#define SHT30_SDA_GPIO             GPIO_NUM_21
#define SHT30_SCL_GPIO             GPIO_NUM_22
#define SHT30_ADDRESS              SHT30_I2C_ADDRESS_DEFAULT
#define SHT30_SAMPLE_INTERVAL_MS   2000

static const char *TAG = "servo_demo";
static const char *RAIN_TAG = "rain_sensor";
static const char *SHT30_TAG = "sht30";

static void sht30_task(void *arg)
{
    (void)arg;
    const i2c_master_bus_config_t bus_config = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = SHT30_SDA_GPIO,
        .scl_io_num = SHT30_SCL_GPIO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    i2c_master_bus_handle_t bus = NULL;
    esp_err_t err = i2c_new_master_bus(&bus_config, &bus);
    if (err != ESP_OK) {
        ESP_LOGE(SHT30_TAG, "I2C bus init failed: %s", esp_err_to_name(err));
        vTaskDelete(NULL);
        return;
    }

    sht30_t sensor = {0};
    ESP_LOGI(SHT30_TAG, "SDA GPIO%d, SCL GPIO%d, address 0x%02X",
             SHT30_SDA_GPIO, SHT30_SCL_GPIO, SHT30_ADDRESS);
    while (1) {
        if (!sensor.initialized) {
            /* 上次初始化失败且清理未完成时，先重试清理，避免重复添加设备。 */
            if (sensor.i2c_dev != NULL) {
                err = sht30_deinit(&sensor);
                if (err != ESP_OK) {
                    ESP_LOGW(SHT30_TAG, "Cleanup failed: %s", esp_err_to_name(err));
                    vTaskDelay(pdMS_TO_TICKS(SHT30_SAMPLE_INTERVAL_MS));
                    continue;
                }
            }
            err = sht30_init(&sensor, bus, SHT30_ADDRESS);
            if (err != ESP_OK) {
                ESP_LOGW(SHT30_TAG, "Sensor init failed: %s; retry in 2s",
                         esp_err_to_name(err));
                vTaskDelay(pdMS_TO_TICKS(SHT30_SAMPLE_INTERVAL_MS));
                continue;
            }
        }

        float temperature_c;
        float humidity_percent;
        err = sht30_read(&sensor, &temperature_c, &humidity_percent);
        if (err == ESP_OK) {
            ESP_LOGI(SHT30_TAG, "Temperature: %.2f C, Humidity: %.2f %%RH",
                     temperature_c, humidity_percent);
        } else {
            ESP_LOGW(SHT30_TAG, "Read failed: %s; retry in 2s", esp_err_to_name(err));
        }
        vTaskDelay(pdMS_TO_TICKS(SHT30_SAMPLE_INTERVAL_MS));
    }
}

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
    if (xTaskCreate(sht30_task, "sht30", 3072, NULL, 5, NULL) != pdPASS) {
        ESP_LOGE(SHT30_TAG, "Failed to create SHT30 task");
    }
    if (xTaskCreate(rain_sensor_task, "rain_sensor", 3072, NULL, 5, NULL) != pdPASS) {
        ESP_LOGE(RAIN_TAG, "Failed to create rain sensor task");
    }
    // 舵机仍运行原来的往返测试；雨滴任务目前只检测并输出状态。
    if (xTaskCreate(servo_test_task, "servo_test", 3072, NULL, 5, NULL) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create servo test task");
    }
}
