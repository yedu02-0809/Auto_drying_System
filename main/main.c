#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "servo.h"
#include "rain_sensor.h"
#include "sht30.h"
#include "key.h"
#include "esp_timer.h"

#define SERVO_GPIO          GPIO_NUM_18
#define SERVO_CENTER_ANGLE  90
#define SERVO_RETRACT_ANGLE 45
#define SERVO_EXTEND_ANGLE  135
#define SERVO_STEP_DELAY_MS 20

/* 常开按键另一端接 GND；上拉输入，按下为低电平。 */
#define KEY_EXTEND_GPIO     GPIO_NUM_25
#define KEY_RETRACT_GPIO    GPIO_NUM_26
#define KEY_SCAN_INTERVAL_MS 10

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

static const char *TAG = "manual_control";
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

/* 按键扫描和舵机控制在同一任务执行，避免多个任务同时修改 LEDC。
 * 每次最多下发 1 度变化，移动期间继续扫描按键，支持中途改变方向。
 * current_angle 是最后下发的目标角度，SG90 没有实际位置反馈。 */
static void manual_control_task(void *arg)
{
    (void)arg;
    key_pair_t keys = {0};
    esp_err_t err = key_init(&keys, KEY_EXTEND_GPIO, KEY_RETRACT_GPIO);
    if (err == ESP_OK) {
        err = servo_init(SERVO_GPIO);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Control init failed: %s", esp_err_to_name(err));
        vTaskDelete(NULL);
        return;
    }

    int current_angle = SERVO_CENTER_ANGLE;
    int target_angle = SERVO_CENTER_ANGLE;
    int64_t last_step_us = esp_timer_get_time();
    ESP_LOGI(TAG, "Extend key GPIO%d -> %d deg, retract key GPIO%d -> %d deg",
             KEY_EXTEND_GPIO, SERVO_EXTEND_ANGLE, KEY_RETRACT_GPIO, SERVO_RETRACT_ANGLE);

    while (1) {
        key_event_t event;
        err = key_poll(&keys, &event);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Key read failed: %s", esp_err_to_name(err));
            target_angle = current_angle;
        } else if (event != KEY_EVENT_NONE) {
            target_angle = (event == KEY_EVENT_RETRACT)
                ? SERVO_RETRACT_ANGLE : SERVO_EXTEND_ANGLE;
            ESP_LOGI(TAG, "%s: target %d deg",
                     event == KEY_EVENT_RETRACT ? "Retract" : "Extend", target_angle);
        }

        const int64_t now = esp_timer_get_time();
        if (current_angle != target_angle &&
            now - last_step_us >= SERVO_STEP_DELAY_MS * 1000) {
            const int next_angle = current_angle + ((current_angle < target_angle) ? 1 : -1);
            err = servo_set_angle((float)next_angle);
            if (err == ESP_OK) {
                current_angle = next_angle;
            } else {
                // 停止继续下发运动指令，下一次按键可重新尝试。
                target_angle = current_angle;
                ESP_LOGE(TAG, "Servo command failed: %s", esp_err_to_name(err));
            }
            last_step_us = now;
        }
        vTaskDelay(pdMS_TO_TICKS(KEY_SCAN_INTERVAL_MS));
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
    // 两个按键控制舵机；雨滴和温湿度任务目前只检测并输出状态。
    if (xTaskCreate(manual_control_task, "manual_control", 3072, NULL, 5, NULL) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create manual control task");
    }
}
