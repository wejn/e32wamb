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

typedef struct {
    bool valid;
    uint8_t red;
    uint8_t green;
    uint8_t blue;
    uint16_t delay;
} indicator_frame;

static void indicator_led_task(void *pvParameters) {
    float max_brightness = RGB_INDICATOR_MAX_BRIGHTNESS;
#define MAX_FRAMES 2
    indicator_frame frames[][MAX_FRAMES] = {
        {{true, 255, 0, 0, 500}, {true, 64, 0, 0, 500}}, // IS_initial
        {{true, 0, 0, 255, 500}, {true, 0, 0, 64, 500}}, // IS_commissioning
        {{true, 64, 64, 0, 200}, {true, 0, 0, 0, 4800}}, // IS_connected_no_coord
        {{true, 0, 64, 0, 20}, {true, 0, 0, 0, 4980}}, // IS_connected
    };
    uint8_t current_frame = MAX_FRAMES - 1;
    indicator_frame *f = NULL;
    while (true) {
        f = frames[il_state];
        if (current_frame + 1 > MAX_FRAMES - 1 || !f[current_frame + 1].valid) {
            current_frame = 0;
        } else {
            current_frame++;
        }
        f = &f[current_frame];
        led_strip_set_pixel(il_led_strip, 0, f->red * max_brightness, f->green * max_brightness, f->blue * max_brightness);
        led_strip_refresh(il_led_strip);
        xTaskNotifyWait(0, 0, NULL, pdMS_TO_TICKS(f->delay));
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
