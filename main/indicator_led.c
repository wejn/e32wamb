/*
 * ESP32 White Ambiance
 * Written in 2025 by Michal Jirků (wejn)
 *
 * This code is licensed under AGPL version 3.
 */
#include "esp_check.h"
#include "global_config.h"
#include "indicator_led.h"
#include "led_strip.h"

static const char *TAG = "INDICATOR_LED";
static TaskHandle_t il_task_handle;
volatile static bool il_initialized = false;
volatile static indicator_state il_state = IS_initial;
static led_strip_handle_t il_led_strip;

static void indicator_led_task(void *pvParameters) {
    float max_brightness = RGB_INDICATOR_MAX_BRIGHTNESS;
    while (true) {
        switch (il_state) {
            case IS_initial:
                led_strip_set_pixel(il_led_strip, 0, 255 * max_brightness, 255 * max_brightness, 255 * max_brightness);
                break;
            case IS_commissioning:
                led_strip_set_pixel(il_led_strip, 0, 255 * max_brightness, 0, 0);
                // FIXME: make this state a blink
                break;
            case IS_connected_no_coord:
                led_strip_set_pixel(il_led_strip, 0, 255 * max_brightness, 255 * max_brightness, 0);
                // FIXME: make this state a (short?) blink
                break;
            case IS_connected:
                led_strip_set_pixel(il_led_strip, 0, 0, 255 * max_brightness, 0);
                // FIXME: make default state a short blink
                break;
        }
        led_strip_refresh(il_led_strip);
        xTaskNotifyWait(0, 0, NULL, portMAX_DELAY);
    }
}

esp_err_t indicator_led_initialize() {
    esp_err_t ret = ESP_OK;

    if (il_initialized) {
        ESP_LOGW(TAG, "Attempted to initialize indicator led more than once");
    } else {
        led_strip_config_t strip_config = {
            .strip_gpio_num = RGB_INDICATOR_GPIO,
            .max_leds = 1,
            .led_model = RGB_INDICATOR_MODEL,
            .color_component_format = RGB_INDICATOR_FORMAT,
            .flags = {
                .invert_out = false,
            }
        };

        led_strip_rmt_config_t rmt_config = {
            .clk_src = RMT_CLK_SRC_DEFAULT,
            .resolution_hz = 10 * 1000 * 1000,
            .mem_block_symbols = 64,
            .flags = {
                .with_dma = false,
            }
        };

        ret = led_strip_new_rmt_device(&strip_config, &rmt_config, &il_led_strip);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "can't initialize indicator led: %s", esp_err_to_name(ret));
        } else {
            il_initialized = true;
            xTaskCreate(indicator_led_task, "indicator_led", 4096, NULL, 0, &il_task_handle);
            ESP_LOGI(TAG, "Initialized");
        }
    }

    return ret;
}

esp_err_t indicator_led_switch(indicator_state state) {
    if (!il_initialized) {
        ESP_LOGE(TAG, "Update triggered without initialization, skip");
        return ESP_ERR_NOT_SUPPORTED;
    } else {
        il_state = state;
        xTaskNotifyGive(il_task_handle);
    }

    return ESP_OK;
}
