/*
 * ESP32 White Ambiance
 * Copyright © 2025 Michal Jirků (wejn)
 *
 * This code is licensed under GPL version 3.
 */
#include <string.h>

#include "esp_check.h"
#include "esp_err.h"

#include "light_config.h"
#include "scenes.h"

static const char *TAG = "SCENES";

esp_err_t store_scene(esp_zb_zcl_store_scene_message_t *msg) {
    esp_err_t err = ESP_FAIL;

    ESP_RETURN_ON_FALSE(msg, ESP_FAIL, TAG, "Empty message");
    ESP_RETURN_ON_FALSE(msg->info.status == ESP_ZB_ZCL_STATUS_SUCCESS, ESP_ERR_INVALID_ARG, TAG,
            "Received message: error status(%d)", msg->info.status);
    ESP_LOGI(TAG, "Store scene %d for group %d", msg->scene_id, msg->group_id);

    uint8_t onoff = light_config->onoff;
    uint8_t level = light_config->level;
    uint16_t temperature = light_config->temperature;

    esp_zb_zcl_scenes_extension_field_t onoff_field = {
        .cluster_id = ESP_ZB_ZCL_CLUSTER_ID_ON_OFF,
        .length = sizeof(uint8_t),
        .extension_field_attribute_value_list = &onoff,
        .next = NULL,
    };

    esp_zb_zcl_scenes_extension_field_t level_field = {
        .cluster_id = ESP_ZB_ZCL_CLUSTER_ID_LEVEL_CONTROL,
        .length = sizeof(uint8_t),
        .extension_field_attribute_value_list = &level,
        .next = &onoff_field,
    };

    esp_zb_zcl_scenes_extension_field_t color_field = {
        .cluster_id = ESP_ZB_ZCL_CLUSTER_ID_COLOR_CONTROL,
        .length = sizeof(uint16_t),
        .extension_field_attribute_value_list = (uint8_t*) &temperature,
        .next = &level_field,
    };

    err = esp_zb_zcl_scenes_table_store(msg->info.dst_endpoint, msg->group_id, msg->scene_id, 0x0000, &color_field);
    esp_zb_zcl_scenes_table_show(msg->info.dst_endpoint);

    return err;
}

esp_err_t recall_scene(esp_zb_zcl_recall_scene_message_t *msg) {
    esp_err_t err = ESP_OK;

    ESP_RETURN_ON_FALSE(msg, ESP_FAIL, TAG, "Empty message");
    ESP_RETURN_ON_FALSE(msg->info.status == ESP_ZB_ZCL_STATUS_SUCCESS, ESP_ERR_INVALID_ARG, TAG,
            "Received message: error status(%d)", msg->info.status);
    ESP_LOGI(TAG, "Recall scene %d for group %d", msg->scene_id, msg->group_id);

    esp_zb_zcl_scenes_extension_field_t *f = msg->field_set;

    while (f) {
        switch (f->cluster_id) {
            case ESP_ZB_ZCL_CLUSTER_ID_ON_OFF:
                light_config_update(LCFV_onoff, *(uint8_t*) f->extension_field_attribute_value_list);
                esp_zb_zcl_set_attribute_val(
                        msg->info.dst_endpoint,
                        ESP_ZB_ZCL_CLUSTER_ID_ON_OFF,
                        ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
                        ESP_ZB_ZCL_ATTR_ON_OFF_ON_OFF_ID,
                        f->extension_field_attribute_value_list,
                        false);
                break;
            case ESP_ZB_ZCL_CLUSTER_ID_LEVEL_CONTROL:
                light_config_update(LCFV_level, *(uint8_t*) f->extension_field_attribute_value_list);
                esp_zb_zcl_set_attribute_val(
                        msg->info.dst_endpoint,
                        ESP_ZB_ZCL_CLUSTER_ID_LEVEL_CONTROL,
                        ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
                        ESP_ZB_ZCL_ATTR_LEVEL_CONTROL_CURRENT_LEVEL_ID,
                        f->extension_field_attribute_value_list,
                        false);
                break;
            case ESP_ZB_ZCL_CLUSTER_ID_COLOR_CONTROL:
                light_config_update(LCFV_temperature, *(uint16_t*) f->extension_field_attribute_value_list);
                esp_zb_zcl_set_attribute_val(
                        msg->info.dst_endpoint,
                        ESP_ZB_ZCL_CLUSTER_ID_COLOR_CONTROL,
                        ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
                        ESP_ZB_ZCL_ATTR_COLOR_CONTROL_COLOR_TEMPERATURE_ID,
                        f->extension_field_attribute_value_list,
                        false);
                break;
            default:
                ESP_LOGW(TAG, "Unknown field(s) to recall for endpoint %d, cluster %d", msg->info.dst_endpoint, f->cluster_id);
                break;
        }
        f = f->next;
    }

    return err;
}
