/*
 * ESP32 White Ambiance
 * Copyright © 2025 Michal Jirků (wejn)
 *
 * This code is licensed under GPL version 3.
 */
#include "driver/gpio.h"
#include "esp_check.h"

#include "global_config.h"
#include "rfswitch.h"

static const char *TAG = "RF_SWITCH";

esp_err_t rf_switch_initialize(bool external) {
  esp_err_t ret = ESP_OK;

  if (RF_SWITCH_GPIO < 0) {
    ESP_LOGI(TAG, "NOT configuring (disabled)");
    return ESP_OK;
  }

  ESP_LOGI(TAG, "configuring on gpio %d", RF_SWITCH_GPIO);

  gpio_config_t io_conf = {
    .intr_type = GPIO_INTR_DISABLE,
    .mode = GPIO_MODE_OUTPUT,
    .pin_bit_mask = 1ULL<<RF_SWITCH_GPIO,
    .pull_down_en = !external,
    .pull_up_en = external,
  };

  ret = gpio_config(&io_conf);
  if (ret != ESP_OK) {
    ESP_LOGW(TAG, "can't configure: %s", esp_err_to_name(ret));
    return ret;
  }

  return rf_switch_set(external);
}

esp_err_t rf_switch_set(bool external) {
  ESP_LOGI(TAG, "setting to %s antenna", external ? "u.fl" : "built-in");
  esp_err_t ret = gpio_set_level(RF_SWITCH_GPIO, external);

  if (ret != ESP_OK) {
    ESP_LOGW(TAG, "can't set: %s", esp_err_to_name(ret));
  }

  return ret;
}
