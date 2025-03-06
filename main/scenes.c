/*
 * ESP32 White Ambiance
 * Written in 2025 by Michal Jirků (wejn)
 *
 * This code is licensed under AGPL version 3.
 */
#include "esp_check.h"
#include "string.h"
#include "light_state.h"
#include "delayed_save.h"
#include "scenes.h"
#include "esp_err.h"

static const char *TAG = "SCENES";

esp_err_t store_scene(esp_zb_zcl_store_scene_message_t *msg) {
    esp_err_t err = ESP_FAIL;

    ESP_RETURN_ON_FALSE(msg, ESP_FAIL, TAG, "Empty message");
    ESP_RETURN_ON_FALSE(msg->info.status == ESP_ZB_ZCL_STATUS_SUCCESS, ESP_ERR_INVALID_ARG, TAG,
            "Received message: error status(%d)", msg->info.status);
    ESP_LOGI(TAG, "Store scene %d for group %d", msg->scene_id, msg->group_id);

    uint8_t onoff = g_onoff;
    uint8_t level = g_level;
    uint16_t temperature = g_temperature;

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
                // FIXME: sync with attribute handler
                g_onoff = (bool) *(uint8_t*) f->extension_field_attribute_value_list;
                esp_zb_zcl_set_attribute_val(
                        msg->info.dst_endpoint,
                        ESP_ZB_ZCL_CLUSTER_ID_ON_OFF,
                        ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
                        ESP_ZB_ZCL_ATTR_ON_OFF_ON_OFF_ID,
                        f->extension_field_attribute_value_list,
                        false);
                trigger_delayed_save(DS_onoff);
                break;
            case ESP_ZB_ZCL_CLUSTER_ID_LEVEL_CONTROL:
                // FIXME: sync with attribute handler
                g_level = *(uint8_t*) f->extension_field_attribute_value_list;
                esp_zb_zcl_set_attribute_val(
                        msg->info.dst_endpoint,
                        ESP_ZB_ZCL_CLUSTER_ID_LEVEL_CONTROL,
                        ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
                        ESP_ZB_ZCL_ATTR_LEVEL_CONTROL_CURRENT_LEVEL_ID,
                        f->extension_field_attribute_value_list,
                        false);
                trigger_delayed_save(DS_level);
                break;
            case ESP_ZB_ZCL_CLUSTER_ID_COLOR_CONTROL:
                // FIXME: sync with attribute handler
                g_temperature = *(uint16_t*) f->extension_field_attribute_value_list;
                esp_zb_zcl_set_attribute_val(
                        msg->info.dst_endpoint,
                        ESP_ZB_ZCL_CLUSTER_ID_COLOR_CONTROL,
                        ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
                        ESP_ZB_ZCL_ATTR_COLOR_CONTROL_COLOR_TEMPERATURE_ID,
                        f->extension_field_attribute_value_list,
                        false);
                trigger_delayed_save(DS_temperature);
                break;
            default:
                ESP_LOGW(TAG, "Unknown field(s) to recall for endpoint %d, cluster %d", msg->info.dst_endpoint, f->cluster_id);
                break;
        }
        f = f->next;
    }

    return err;
}
