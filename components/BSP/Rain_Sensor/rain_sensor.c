#include "rain_sensor.h"

#include <stddef.h>
#include <stdint.h>

static gpio_num_t s_gpio = GPIO_NUM_NC;
static int s_active_level;

esp_err_t rain_sensor_init(gpio_num_t gpio_num, int active_level)
{
    if (s_gpio != GPIO_NUM_NC) {
        return ESP_ERR_INVALID_STATE;
    }
    /* 先检查范围，避免 GPIO 检查宏对越界编号进行位移。 */
    if (gpio_num < 0 || gpio_num >= GPIO_NUM_MAX ||
        !GPIO_IS_VALID_GPIO(gpio_num) ||
        (active_level != 0 && active_level != 1)) {
        return ESP_ERR_INVALID_ARG;
    }

    /* 当前 ESP32 的 GPIO34~39 没有内部上下拉，需要模块或外部电阻
     * 提供稳定电平。其他 GPIO 默认偏置到“无雨”电平。 */
    const bool has_internal_pull = GPIO_IS_VALID_OUTPUT_GPIO(gpio_num);
    const gpio_config_t config = {
        .pin_bit_mask = 1ULL << gpio_num,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = (has_internal_pull && active_level == 0)
            ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE,
        .pull_down_en = (has_internal_pull && active_level == 1)
            ? GPIO_PULLDOWN_ENABLE : GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&config);
    if (err != ESP_OK) {
        return err;
    }

    s_active_level = active_level;
    s_gpio = gpio_num;
    return ESP_OK;
}

esp_err_t rain_sensor_read(bool *is_raining)
{
    if (is_raining == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_gpio == GPIO_NUM_NC) {
        return ESP_ERR_INVALID_STATE;
    }

    *is_raining = gpio_get_level(s_gpio) == s_active_level;
    return ESP_OK;
}
