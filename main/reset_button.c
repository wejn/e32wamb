/*
 * ESP32 White Ambiance
 * Copyright © 2025 Michal Jirků (wejn)
 *
 * This code is licensed under GPL version 3.
 */
#include "esp_check.h"
#include "global_config.h"
#include "reset_button.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "indicator_led.h"
#include "light_config.h"

#define DEBOUNCE_DELAY_US 50 * 1000 // ms in μs
#define LONG_PRESS_DELAY_US 5 * 1000 * 1000 // s in μs
#define SUSPEND_MS 250

static const char *TAG = "RESET_BUTTON";
static TaskHandle_t rb_task_handle;
volatile static bool rb_initialized = false;
volatile static uint64_t press_time = 0;

static void IRAM_ATTR gpio_isr_handler(void* arg) {
    uint32_t gpio_state = gpio_get_level(RESET_BUTTON_GPIO);

    if (gpio_state == 1) { // Pulled up → released
        if (!press_time || esp_timer_get_time() - press_time < DEBOUNCE_DELAY_US) {
            // ignore (not pressed, or bouncing)
        } else {
            // release
            press_time = 0;
        }
    } else { // Shorted to gnd → pressed
        if (press_time == 0) {
            // record press
            press_time = esp_timer_get_time();
            vTaskNotifyGiveFromISR(rb_task_handle, NULL);

        }
    }

}

static void reset_button_task(void *pvParameters) {
    bool locked = false;
    xTaskNotifyWait(0, 0, NULL, portMAX_DELAY); // block immediately ;)
    while (true) {
        if (press_time > 0) {
            if (esp_timer_get_time() - press_time < LONG_PRESS_DELAY_US) {
                ESP_LOGI(TAG, "pressed; keep going...");
                if (!locked) {
                    indicator_led_lock(IS_reset_pending);
                    locked = true;
                }
                xTaskNotifyWait(0, 0, NULL, pdMS_TO_TICKS(SUSPEND_MS));
            } else {
                ESP_LOGI(TAG, "long press -- factory resetting...");
                light_config_erase_flash(); // erase all config from flash
                esp_zb_factory_reset();
                xTaskNotifyWait(0, 0, NULL, portMAX_DELAY);
            }

        } else {
            ESP_LOGI(TAG, "abort");
            if (locked) {
                indicator_led_unlock();
                locked = false;
            }
            xTaskNotifyWait(0, 0, NULL, portMAX_DELAY);
        }
    }
}

esp_err_t reset_button_initialize() {
    esp_err_t ret = ESP_OK;

    if (rb_initialized) {
        ESP_LOGW(TAG, "Attempted to initialize reset button more than once");
    } else {
        gpio_config_t io_conf = {};
        io_conf.intr_type = GPIO_INTR_ANYEDGE;
        io_conf.pin_bit_mask = 1ULL<<RESET_BUTTON_GPIO;
        io_conf.mode = GPIO_MODE_INPUT;
        io_conf.pull_up_en = 1;
        ret = gpio_config(&io_conf);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "can't configure the gpio: %s", esp_err_to_name(ret));
            return ret;
        }

        ret = gpio_install_isr_service(0);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "can't install the isr service: %s", esp_err_to_name(ret));
            return ret;
        }
        ret = gpio_isr_handler_add(RESET_BUTTON_GPIO, gpio_isr_handler, NULL);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "can't add the isr handler: %s", esp_err_to_name(ret));
            return ret;
        }

        rb_initialized = true;
        xTaskCreate(reset_button_task, "reset_button", 4096, NULL, 0, &rb_task_handle);
        ESP_LOGI(TAG, "Initialized");
    }

    return ret;
}
