/*
 * ESP32 White Ambiance
 * Copyright © 2025 Michal Jirků (wejn)
 *
 * This code is licensed under GPL version 3.
 */
#include "esp_check.h"
#include "esp_zigbee_core.h"
#include "indicator_led.h"
#include "status_indicator.h"
#include "esp_timer.h"
#include "main.h"

#define INDICATOR_REFRESH_MS 1000
#define QUERYING_TIMEOUT 30 * 1000 * 1000 // s in μs

static const char *TAG = "STATUS_INDICATOR";

static void status_indicator_task(void *pvParameters) {
    indicator_state state = IS_initial;

    while (true) {
        if (!esp_zb_is_started() || esp_zb_get_bdb_commissioning_mode() & ESP_ZB_BDB_NETWORK_FORMATION) {
            // If the CM is network formation, we're not configured yet
            if (state != IS_initial) {
                ESP_LOGI(TAG, "setting as initial");
                state = IS_initial;
                indicator_led_switch(IS_initial);
            }
        } else {
            // If the CM is zero (no commissioning) or we've actually joined network → definitely not commissioning
            if (esp_zb_get_bdb_commissioning_mode() == 0 || esp_zb_bdb_dev_joined()) {
                esp_zb_nwk_info_iterator_t it = ESP_ZB_NWK_INFO_ITERATOR_INIT;
                esp_zb_nwk_neighbor_info_t neighbor = {};
                bool have_coord = false;
                bool have_reader = false;

                // If there were recent queries on the light endpoint, we're not alone...
                if ((esp_timer_get_time() - light_endpoint_last_queried_time) < QUERYING_TIMEOUT) {
                    have_reader = true;
                }

                // If no reader (by queries), then check neighbor table...
                if(!have_reader && esp_zb_lock_acquire(portMAX_DELAY)) {
                    while (ESP_OK == esp_zb_nwk_get_next_neighbor(&it, &neighbor)) {
                        if (neighbor.device_type == ESP_ZB_DEVICE_TYPE_COORDINATOR) {
                            // Normal coordinator. \o/
                            have_coord = true;
                            break;
                        }
                    }

                    esp_zb_lock_release();
                }

                if (have_coord || have_reader) {
                    if (state != IS_connected) {
                        if (have_reader) {
                            ESP_LOGI(TAG, "was recently queried -- assuming online");
                        }
                        if (have_coord) {
                            ESP_LOGI(TAG, "online: found coordinator: 0x%04hx, age: %d, lqi: %d, type: %d", neighbor.short_addr, neighbor.age, neighbor.lqi, neighbor.device_type);
                        }
                        state = IS_connected;
                        indicator_led_switch(IS_connected);
                    }
                } else {
                    if (state != IS_connected_no_coord) {
                        ESP_LOGI(TAG, "connected but offline: no coordinator present, and no recent queries");
                        state = IS_connected_no_coord;
                        indicator_led_switch(IS_connected_no_coord);
                    }
                }

            } else {
                // If CM is not success, then we're in the process of commissioning...
                if (esp_zb_get_bdb_commissioning_status() != ESP_ZB_BDB_STATUS_SUCCESS) {
                    if (state != IS_commissioning) {
                        ESP_LOGI(TAG, "Status: Commissioning");
                        state = IS_commissioning;
                        indicator_led_switch(IS_commissioning);
                    }
                }
            }
        }

        vTaskDelay(INDICATOR_REFRESH_MS / portTICK_PERIOD_MS);
    }
}

esp_err_t status_indicator_initialize() {
    esp_err_t ret = ESP_OK;

    ret = indicator_led_initialize();

    if (ret == ESP_OK) {
        xTaskCreate(status_indicator_task, "status_indicator", 4096, NULL, 2, NULL);
    }

    return ret;
}
