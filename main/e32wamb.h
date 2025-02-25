/*
 * ESP32 White Ambiance
 * Written in 2025 by Michal Jirků (wejn)
 *
 * This code is licensed under AGPL version 3.
 * Originally forked from https://github.com/wejn/esp32-huello-world.
 */

#include "esp_zigbee_core.h"
#include "light_driver.h"
#include "basic_cluster.h"

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

#define MY_DEVICE_ID 0x010c // Color Temperature Light, as per ZB Doc 15-0014-05, p.22

#define MY_EP_CONFIG() { \
    .endpoint = MY_LIGHT_ENDPOINT, \
    .app_profile_id = ESP_ZB_AF_HA_PROFILE_ID, \
    .app_device_id = MY_DEVICE_ID, \
    .app_device_version = 2, \
}

#define MY_BASIC_INFO() { \
    .manufacturer_name = "\x08""wejn.org", \
    .model_identifier = "\x07""e32wamb", \
    .date_code = "\x08""20250216", \
}

#define MY_LIGHT_CONFIG()                                                                \
    {                                                                                    \
        .basic_cfg =                                                                     \
            {                                                                            \
                .zcl_version = ESP_ZB_ZCL_BASIC_ZCL_VERSION_DEFAULT_VALUE,               \
                .power_source = 0x01,                                                    \
            },                                                                           \
        .identify_cfg =                                                                  \
            {                                                                            \
                .identify_time = ESP_ZB_ZCL_IDENTIFY_IDENTIFY_TIME_DEFAULT_VALUE,        \
            },                                                                           \
        .groups_cfg =                                                                    \
            {                                                                            \
                .groups_name_support_id = ESP_ZB_ZCL_GROUPS_NAME_SUPPORT_DEFAULT_VALUE,  \
            },                                                                           \
        .scenes_cfg =                                                                    \
            {                                                                            \
                .scenes_count = ESP_ZB_ZCL_SCENES_SCENE_COUNT_DEFAULT_VALUE,             \
                .current_scene = ESP_ZB_ZCL_SCENES_CURRENT_SCENE_DEFAULT_VALUE,          \
                .current_group = ESP_ZB_ZCL_SCENES_CURRENT_GROUP_DEFAULT_VALUE,          \
                .scene_valid = ESP_ZB_ZCL_SCENES_SCENE_VALID_DEFAULT_VALUE,              \
                .name_support = ESP_ZB_ZCL_SCENES_NAME_SUPPORT_DEFAULT_VALUE,            \
            },                                                                           \
        .on_off_cfg =                                                                    \
            {                                                                            \
                .on_off = 1,                                                             \
            },                                                                           \
        .level_cfg =                                                                     \
            {                                                                            \
                .current_level = 254,                                                    \
            },                                                                           \
        .color_cfg =                                                                     \
            {                                                                            \
                .current_x = ESP_ZB_ZCL_COLOR_CONTROL_CURRENT_X_DEF_VALUE,               \
                .current_y = ESP_ZB_ZCL_COLOR_CONTROL_CURRENT_Y_DEF_VALUE,               \
                .color_mode = 0x02,                                                      \
                .options = ESP_ZB_ZCL_COLOR_CONTROL_OPTIONS_DEFAULT_VALUE,               \
                .enhanced_color_mode = 0x02,                                             \
                .color_capabilities = 0x0010,                                            \
            },                                                                           \
    }
// FIXME: the current_x, current_y don't make sense given the rest of the setup. Can we avoid?
// FIXME: we're also missing bunch of non-optional stuff from 075123 p. 5-5 (375)
