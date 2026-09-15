#include "servo.h"

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include "driver/ledc.h"

/* SG90 使用 50Hz PWM，即每周期 20000us。
 * 初次测试使用较保守的 1000~2000us 脉宽；实际角度需实物标定。
 * 不要未经测试就扩大到 500~2500us，避免顶到机械限位。 */
#define SERVO_FREQ_HZ        50
#define SERVO_PERIOD_US      20000U
#define SERVO_MIN_PULSE_US   1000U
#define SERVO_MAX_PULSE_US   2000U
#define SERVO_DUTY_BITS      14
#define SERVO_MODE           LEDC_LOW_SPEED_MODE
#define SERVO_TIMER          LEDC_TIMER_0
#define SERVO_CHANNEL        LEDC_CHANNEL_0

static bool s_initialized;

static uint32_t angle_to_duty(float angle)
{
    const float pulse_us = SERVO_MIN_PULSE_US +
        (SERVO_MAX_PULSE_US - SERVO_MIN_PULSE_US) * angle / 180.0f;
    return (uint32_t)(pulse_us * (1U << SERVO_DUTY_BITS) / SERVO_PERIOD_US + 0.5f);
}

esp_err_t servo_init(gpio_num_t gpio_num)
{
    if (s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    /* 先限制范围，避免 GPIO 检查宏对过大的编号进行越界位移。 */
    if (gpio_num < 0 || gpio_num >= GPIO_NUM_MAX ||
        !GPIO_IS_VALID_OUTPUT_GPIO(gpio_num)) {
        return ESP_ERR_INVALID_ARG;
    }

    const ledc_timer_config_t timer = {
        .speed_mode = SERVO_MODE,
        .duty_resolution = LEDC_TIMER_14_BIT,
        .timer_num = SERVO_TIMER,
        .freq_hz = SERVO_FREQ_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    esp_err_t err = ledc_timer_config(&timer);
    if (err != ESP_OK) {
        return err;
    }

    const ledc_channel_config_t channel = {
        .gpio_num = gpio_num,
        .speed_mode = SERVO_MODE,
        .channel = SERVO_CHANNEL,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = SERVO_TIMER,
        .duty = angle_to_duty(90.0f),  // 初始输出 1500us，中间位置
        .hpoint = 0,
    };
    err = ledc_channel_config(&channel);
    if (err == ESP_OK) {
        s_initialized = true;
    }
    return err;
}

esp_err_t servo_set_angle(float angle)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!isfinite(angle) || angle < 0.0f || angle > 180.0f) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = ledc_set_duty(SERVO_MODE, SERVO_CHANNEL, angle_to_duty(angle));
    if (err != ESP_OK) {
        return err;
    }
    return ledc_update_duty(SERVO_MODE, SERVO_CHANNEL);
}
