/*
 * ESP32 White Ambiance
 * Copyright © 2025 Michal Jirků (wejn)
 *
 * This code is licensed under GPL version 3.
 */
#include "esp_check.h"
#include "global_config.h"
#include "light_config.h"
#include "light_driver.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "data_tables.h"

static const char *TAG = "LIGHT_DRIVER";
static TaskHandle_t ld_task_handle;
volatile static bool ld_initialized = false;

#define COLOR_TABLE_SIZE COLOR_MAX_TEMPERATURE - COLOR_MIN_TEMPERATURE + 1
static double color_normal[COLOR_TABLE_SIZE] = COLOR_DATA_NORMAL;
static double color_cold[COLOR_TABLE_SIZE] = COLOR_DATA_COLD;
static double color_warm[COLOR_TABLE_SIZE] = COLOR_DATA_HOT;
static double brightness_normal[256] = BRIGHTNESS_DATA_NORMAL;
static double brightness_cold[256] = BRIGHTNESS_DATA_COLD;
static double brightness_warm[256] = BRIGHTNESS_DATA_HOT;

#define MY_SPD_MODE LEDC_LOW_SPEED_MODE
#define MY_DUTY_RES LEDC_TIMER_13_BIT
#define MAX_DUTY (1 << MY_DUTY_RES) - 1

// As described on https://wejn.org/2025/03/testing-esp32-ledc-nonblocking-fading/
// even the LEDC_FADE_NO_WAIT would block. So let's explicitly stop first,
// then start new fade with 100ms. Why 100ms? Because that's spacing that
// esp-zigbee-sdk uses for color changes.
#define FADE(chan, duty) do { \
    ledc_fade_stop(MY_SPD_MODE, chan); \
    ledc_set_fade_time_and_start(MY_SPD_MODE, chan, duty, 100, LEDC_FADE_NO_WAIT); \
} while(0)
// FIXME: This sometimes triggers assert fail: https://github.com/espressif/esp-idf/issues/15580 (and IMO shouldn't)

static void light_driver_task(void *pvParameters) {
    xTaskNotifyWait(0, 0, NULL, portMAX_DELAY); // block immediately ;)
    while (true) {
        if (! *light_config_initialized) {
            ESP_LOGW(TAG, "The light_config not initialized yet, skip");
        } else {
            // FIXME: maybe also consider different fading durations?
            // (because of OffWithEffect comes with 0.8s...)
            // But that needs additional work: https://github.com/espressif/esp-zigbee-sdk/issues/596
            if (! light_config->onoff) {
                // ESP_LOGI(TAG, "Set to off");
                FADE(LEDC_CHANNEL_0, 0);
                FADE(LEDC_CHANNEL_1, 0);
                FADE(LEDC_CHANNEL_2, 0);
                FADE(LEDC_CHANNEL_3, 0); // XXX: unused
                FADE(LEDC_CHANNEL_4, 0); // XXX: unused
            } else {
                // FIXME: take light_config->level_options&2 into consideration!
                double normal = color_normal[light_config->temperature - COLOR_MIN_TEMPERATURE] * brightness_normal[light_config->level];
                double cold = color_cold[light_config->temperature - COLOR_MIN_TEMPERATURE] * brightness_cold[light_config->level];
                double warm = color_warm[light_config->temperature - COLOR_MIN_TEMPERATURE] * brightness_warm[light_config->level];
                // ESP_LOGI(TAG, "Set to %.04f, %.04f, %.04f", normal, cold, warm);
                FADE(LEDC_CHANNEL_0, MAX_DUTY * normal);
                FADE(LEDC_CHANNEL_1, MAX_DUTY * cold);
                FADE(LEDC_CHANNEL_2, MAX_DUTY * warm);
                FADE(LEDC_CHANNEL_3, 0); // XXX: unused
                FADE(LEDC_CHANNEL_4, 0); // XXX: unused
            }
        }
        xTaskNotifyWait(0, 0, NULL, portMAX_DELAY);
    }
}

esp_err_t light_driver_update() {
    if (!ld_initialized) {
        ESP_LOGE(TAG, "Update triggered without initialization, skip");
        return ESP_ERR_NOT_SUPPORTED;
    } else {
        xTaskNotifyGive(ld_task_handle);
    }

    return ESP_OK;
}

#define CONFIG_CHAN(PIN, NUM) do { \
        ledc_channel_config_t chan_##NUM = { \
            .speed_mode = MY_SPD_MODE, \
            .channel = LEDC_CHANNEL_##NUM, \
            .timer_sel = timer, \
            .intr_type = LEDC_INTR_DISABLE, /* .intr_type: rethink if you want fading; esp-idf/examples/peripherals/ledc/ledc_fade/main/ledc_fade_example_main.c */ \
            .gpio_num = PIN, \
            .duty = 0, \
            .hpoint = 0, /* ?? */ \
            .flags.output_invert = 0, \
        }; \
        ret = ledc_channel_config(&chan_##NUM); \
        if (ret != ESP_OK) { \
            ESP_LOGE(TAG, "can't config ledc chan %d: %s, abort", NUM, esp_err_to_name(ret)); \
            return ret; \
        } \
} while(0)

esp_err_t light_driver_initialize() {
    esp_err_t ret = ESP_OK;

    if (ld_initialized) {
        ESP_LOGW(TAG, "Attempted to initialize light driver more than once");
    } else {
        gpio_config_t io_conf = {
            .intr_type = GPIO_INTR_DISABLE,
            .mode = GPIO_MODE_OUTPUT,
            .pin_bit_mask = 1ULL<<MY_LIGHT_PWM_CH0_GPIO | 1ULL<<MY_LIGHT_PWM_CH1_GPIO | 1ULL<<MY_LIGHT_PWM_CH2_GPIO |
                1ULL<<MY_LIGHT_PWM_CH3_GPIO | 1ULL<<MY_LIGHT_PWM_CH4_GPIO,
            .pull_down_en = 1,
            .pull_up_en = 0,
        };
        ret = gpio_config(&io_conf);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "gpio_config failed with: %s", esp_err_to_name(ret));
        }

        ledc_timer_t timer = LEDC_TIMER_0;

        ledc_timer_config_t ledc_timer = {
            .speed_mode = MY_SPD_MODE,
            .timer_num = timer,
            .duty_resolution = MY_DUTY_RES,
            .freq_hz = 5000, // FIXME: Maybe 1k like Hue?
            .clk_cfg = LEDC_AUTO_CLK,
        };
        ret = ledc_timer_config(&ledc_timer);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "can't config ledc: %s, abort", esp_err_to_name(ret));
            return ret;
        }

        ret = ledc_fade_func_install(ESP_INTR_FLAG_LEVEL3); // higher prio -> buttery smooth?
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "can't install fade func: %s, abort", esp_err_to_name(ret));
            return ret;
        }

        CONFIG_CHAN(MY_LIGHT_PWM_CH0_GPIO, 0);
        CONFIG_CHAN(MY_LIGHT_PWM_CH1_GPIO, 1);
        CONFIG_CHAN(MY_LIGHT_PWM_CH2_GPIO, 2);
        CONFIG_CHAN(MY_LIGHT_PWM_CH3_GPIO, 3);
        CONFIG_CHAN(MY_LIGHT_PWM_CH4_GPIO, 4);

        ld_initialized = true;
        xTaskCreate(light_driver_task, "light_driver", 4096, NULL, 4, &ld_task_handle);
        ESP_LOGI(TAG, "Initialized");
    }

    return ret;
}
