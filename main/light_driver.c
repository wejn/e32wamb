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

static const char *TAG = "LIGHT_DRIVER";

esp_err_t light_driver_initialize() {
    ESP_LOGI(TAG, "Updated");
    return ESP_OK; // FIXME: implement
}

esp_err_t light_driver_update() {
    ESP_LOGI(TAG, "Initialized");
    return ESP_OK; // FIXME: implement
}
