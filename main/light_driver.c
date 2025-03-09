/*
 * ESP32 White Ambiance
 * Written in 2025 by Michal Jirků (wejn)
 *
 * This code is licensed under AGPL version 3.
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

#define MAX_DUTY 8191 // 2**13 - 1

static void light_driver_task(void *pvParameters) {
    while (true) {
        if (! ld_initialized) {
            // no-op
        } else {
            if (! *light_config_initialized) {
                ESP_LOGW(TAG, "The light_config not initialized yet, skip");
            } else {
                if (! light_config->onoff) {
                    // ESP_LOGI(TAG, "Set to off");
                    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 0);
                    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1, 0);
                    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_2, 0);
                    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_3, 0); // XXX: unused
                    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_4, 0); // XXX: unused
                } else {
                    // FIXME: take light_config->level_options&2 into consideration!
                    // FIXME: switching immediately to the new value is terribly jumpy fading into it is going to be better
                    double normal = color_normal[light_config->temperature - COLOR_MIN_TEMPERATURE] * brightness_normal[light_config->level];
                    double cold = color_cold[light_config->temperature - COLOR_MIN_TEMPERATURE] * brightness_cold[light_config->level];
                    double warm = color_warm[light_config->temperature - COLOR_MIN_TEMPERATURE] * brightness_warm[light_config->level];
                    // ESP_LOGI(TAG, "Set to %.04f, %.04f, %.04f", normal, cold, warm);
                    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, MAX_DUTY * normal);
                    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1, MAX_DUTY * cold);
                    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_2, MAX_DUTY * warm);
                    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_3, 0); // XXX: unused
                    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_4, 0); // XXX: unused
                }
                ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
                ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1);
                ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_2);
                ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_3);
                ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_4);
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
            .speed_mode = LEDC_LOW_SPEED_MODE, \
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
        ESP_LOGW(TAG, "Attempted to initialized light driver more than once");
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
            .speed_mode = LEDC_LOW_SPEED_MODE,
            .timer_num = timer,
            .duty_resolution = LEDC_TIMER_13_BIT,
            .freq_hz = 5000, // FIXME: Maybe 1k like Hue?
            .clk_cfg = LEDC_AUTO_CLK,
        };
        ret = ledc_timer_config(&ledc_timer);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "can't config ledc: %s, abort", esp_err_to_name(ret));
            return ret;
        }

        CONFIG_CHAN(MY_LIGHT_PWM_CH0_GPIO, 0);
        CONFIG_CHAN(MY_LIGHT_PWM_CH1_GPIO, 1);
        CONFIG_CHAN(MY_LIGHT_PWM_CH2_GPIO, 2);
        CONFIG_CHAN(MY_LIGHT_PWM_CH3_GPIO, 3);
        CONFIG_CHAN(MY_LIGHT_PWM_CH4_GPIO, 4);


        xTaskCreate(light_driver_task, "light_driver", 4096, NULL, 4, &ld_task_handle);
        ld_initialized = true;
        ESP_LOGI(TAG, "Initialized");
    }

    return ret;
}
