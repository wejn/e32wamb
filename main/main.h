/*
 * ESP32 White Ambiance
 * Written in 2025 by Michal Jirků (wejn)
 *
 * This code is licensed under AGPL version 3.
 * Originally forked from https://github.com/wejn/esp32-huello-world.
 */

#include "esp_zigbee_core.h"
#include "light_driver.h"
#include "light_config.h"

/* Zigbee configuration */
#define MY_LIGHT_ENDPOINT 10

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

#define MY_EP_CONFIG() { \
    .endpoint = MY_LIGHT_ENDPOINT, \
    .app_profile_id = ESP_ZB_AF_HA_PROFILE_ID, \
    .app_device_id = 0x010c, /* Color Temperature Light, as per ZB Doc 15-0014-05, p.22 */ \
    .app_device_version = 1, \
}

#define MY_LIGHT_CONFIG() { \
    .manufacturer_name = "wejn.org", \
    .model_identifier = "e32wamb", \
    .date_code = BUILD_DATE_CODE, \
    .build_id = BUILD_GIT_REV, \
    .power_source = 0x01, \
    .onoff = 1, \
    .startup_onoff = 1, \
    .level_options = 0, \
    .level = 254, \
    .startup_level = 254, \
    .color_options = 0, \
    .temperature = 400, \
    .startup_temperature = 400, \
    .min_temperature = 153, \
    .max_temperature = 454, \
    .couple_min_temperature = 153, \
}
