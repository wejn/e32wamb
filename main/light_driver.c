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

static const char *TAG = "LIGHT_DRIVER";
static TaskHandle_t ld_task_handle;
volatile static bool ld_initialized = false;

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
                    ESP_LOGI(TAG, "Set to off"); // FIXME
                    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 0);
                    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1, 0);
                    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_2, 0);
                    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_3, 0);
                    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_4, 0);
                } else {
                    ESP_LOGI(TAG, "Set to some on"); // FIXME
                    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, MAX_DUTY / 20);
                    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1, MAX_DUTY / 10);
                    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_2, MAX_DUTY / 5);
                    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_3, MAX_DUTY / 2);
                    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_4, MAX_DUTY);
                    // FIXME: implement
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

#define CONFIG_LED_PIN(PIN) do { \
    gpio_reset_pin(PIN); \
    gpio_set_direction(PIN, GPIO_MODE_OUTPUT); \
    gpio_set_level(PIN, 0); \
} while(0)

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
        CONFIG_LED_PIN(MY_LIGHT_PWM_CH0_GPIO);
        CONFIG_LED_PIN(MY_LIGHT_PWM_CH1_GPIO);
        CONFIG_LED_PIN(MY_LIGHT_PWM_CH2_GPIO);
        CONFIG_LED_PIN(MY_LIGHT_PWM_CH3_GPIO);
        CONFIG_LED_PIN(MY_LIGHT_PWM_CH4_GPIO);

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
