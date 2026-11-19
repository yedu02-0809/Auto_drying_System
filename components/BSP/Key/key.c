#include "key.h"

#include <stddef.h>
#include "esp_timer.h"

#define KEY_DEBOUNCE_US 30000

static bool valid_key_gpio(gpio_num_t gpio)
{
    /* 先验证范围，再调用包含位移的 ESP-IDF 检查宏。
     * 当前 ESP32 只有可输出的 GPIO 才具备本驱动需要的内部上拉。 */
    return gpio >= 0 && gpio < GPIO_NUM_MAX && GPIO_IS_VALID_OUTPUT_GPIO(gpio);
}

esp_err_t key_init(key_t *key, gpio_num_t gpio)
{
    if (key == NULL || !valid_key_gpio(gpio)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (key->initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    const gpio_config_t config = {
        .pin_bit_mask = 1ULL << gpio,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&config);
    if (err != ESP_OK) {
        return err;
    }

    const bool pressed = gpio_get_level(gpio) == 0;
    *key = (key_t){
        .gpio = gpio,
        .candidate_pressed = pressed,
        .stable_pressed = pressed,
        .armed = !pressed,
        .changed_at_us = esp_timer_get_time(),
        .initialized = true,
    };
    return ESP_OK;
}

static bool poll_button(key_t *button, int64_t now)
{
    const bool pressed = gpio_get_level(button->gpio) == 0;
    if (pressed != button->candidate_pressed) {
        button->candidate_pressed = pressed;
        button->changed_at_us = now;
    }
    if (button->candidate_pressed == button->stable_pressed ||
        now - button->changed_at_us < KEY_DEBOUNCE_US) {
        return false;
    }

    button->stable_pressed = button->candidate_pressed;
    if (!button->stable_pressed) {
        button->armed = true;
        return false;
    }
    const bool report_press = button->armed;
    button->armed = false;
    return report_press;
}

esp_err_t key_poll(key_t *key, key_event_t *event)
{
    if (key == NULL || event == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!key->initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    const int64_t now = esp_timer_get_time();
    *event = poll_button(key, now) ? KEY_EVENT_PRESSED : KEY_EVENT_NONE;
    return ESP_OK;
}
