/*
 * ESP32 White Ambiance
 * Written in 2025 by Michal Jirků (wejn)
 *
 * This code is licensed under AGPL version 3.
 * Originally forked from https://github.com/wejn/esp32-huello-world.
 */

#include "esp_zigbee_core.h"
#include "light_driver.h"

/* Zigbee configuration */
#define MY_LIGHT_ENDPOINT 10

/* Basic manufacturer information */
#define MY_MANUFACTURER_NAME "\x08""wejn.org"
#define MY_MODEL_IDENTIFIER "\x07""e32wamb"
#define MY_DATE_CODE "\x08""20250216"

#define ESP_ZB_ZR_CONFIG()                         \
    {                                              \
        .esp_zb_role = ESP_ZB_DEVICE_TYPE_ROUTER,  \
        .install_code_policy = false,              \
        .nwk_cfg.zczr_cfg = {                      \
            .max_children = 10,                    \
        },                                         \
    }

#define ESP_ZB_DEFAULT_RADIO_CONFIG()                           \
    {                                                           \
        .radio_mode = ZB_RADIO_MODE_NATIVE,                     \
    }

#define ESP_ZB_DEFAULT_HOST_CONFIG()                            \
    {                                                           \
        .host_connection_mode = ZB_HOST_CONNECTION_MODE_NONE,   \
    }
