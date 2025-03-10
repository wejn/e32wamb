/*
 * ESP32 White Ambiance
 * Written in 2025 by Michal Jirků (wejn)
 *
 * This code is licensed under AGPL version 3.
 *
 * Purpose: Global config for all aspects of the light (zigbee endpoint config,
 * light config, etc).
 */

#pragma once

#include "esp_zigbee_core.h"
#include "light_driver.h"
#include "light_config.h"

/* Zigbee configuration -- mainly used in main.c */
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

#define COLOR_MIN_TEMPERATURE 153
#define COLOR_MAX_TEMPERATURE 454

/* Used to initialize light_config_t in light_config.c */
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
    .temperature = 366, \
    .startup_temperature = 366, \
    .min_temperature = COLOR_MIN_TEMPERATURE, \
    .max_temperature = COLOR_MAX_TEMPERATURE, \
    .couple_min_temperature = COLOR_MIN_TEMPERATURE, \
}

#define RGB_INDICATOR_GPIO 8
#define RGB_INDICATOR_MODEL LED_MODEL_WS2812
#define RGB_INDICATOR_FORMAT LED_STRIP_COLOR_COMPONENT_FMT_RGB
#define RGB_INDICATOR_MAX_BRIGHTNESS 0.2

#define MY_LIGHT_PWM_CH0_GPIO 18 // normal
#define MY_LIGHT_PWM_CH1_GPIO 19 // cold
#define MY_LIGHT_PWM_CH2_GPIO 20 // hot
#define MY_LIGHT_PWM_CH3_GPIO 21 // unused
#define MY_LIGHT_PWM_CH4_GPIO 22 // unused
