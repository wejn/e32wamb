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

#define INDICATOR_REFRESH_MS 1000

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
                if(esp_zb_lock_acquire(portMAX_DELAY)) {
                    while (ESP_OK == esp_zb_nwk_get_next_neighbor(&it, &neighbor)) {
                        if (neighbor.device_type == ESP_ZB_DEVICE_TYPE_COORDINATOR) {
                            // Normal coordinator. \o/
                            have_coord = true;
                            break;
                        }
                        if (neighbor.device_type == ESP_ZB_DEVICE_TYPE_ROUTER && neighbor.short_addr == 0x0001) {
                            // FIXME: && neighbor.ieee_addr is philips range(?)
                            // FIXME: the gotcha here is that in a Philips network, there might not be a coordinator at 0x0000 (?)
                            have_coord = true;
                            break;
                        }
                    }

                    esp_zb_lock_release();
                }

                if (have_coord) {
                    if (state != IS_connected) {
                        ESP_LOGI(TAG, "found coordinator: 0x%04hx, age: %d, lqi: %d, type: %d", neighbor.short_addr, neighbor.age, neighbor.lqi, neighbor.device_type);
                        state = IS_connected;
                        indicator_led_switch(IS_connected);
                    }
                } else {
                    if (state != IS_connected_no_coord) {
                        ESP_LOGI(TAG, "no coordinator present");
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
