/*
 * ESP32 White Ambiance
 * Written in 2025 by Michal Jirků (wejn)
 *
 * This code is licensed under AGPL version 3.
 */
#include "esp_check.h"
#include "string.h"
#include "light_state.h"
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
                esp_zb_zcl_on_off_cmd_t cmd_onoff = {
                    .address_mode = ESP_ZB_APS_ADDR_MODE_16_ENDP_PRESENT,
                    .zcl_basic_cmd.dst_addr_u.addr_short = esp_zb_get_short_address(),
                    .zcl_basic_cmd.src_endpoint = msg->info.dst_endpoint,
                    .zcl_basic_cmd.dst_endpoint = msg->info.dst_endpoint,
                    .on_off_cmd_id = (bool) (*(uint8_t*) f->extension_field_attribute_value_list),
                };
                ESP_LOGI(TAG, "Recall scene %d for group %d: onoff=%d", msg->scene_id, msg->group_id, cmd_onoff.on_off_cmd_id);
                esp_zb_zcl_on_off_cmd_req(&cmd_onoff);
                break;
            case ESP_ZB_ZCL_CLUSTER_ID_LEVEL_CONTROL:
                esp_zb_zcl_move_to_level_cmd_t cmd_level = {
                    .address_mode = ESP_ZB_APS_ADDR_MODE_16_ENDP_PRESENT,
                    .zcl_basic_cmd.dst_addr_u.addr_short = esp_zb_get_short_address(),
                    .zcl_basic_cmd.src_endpoint = msg->info.dst_endpoint,
                    .zcl_basic_cmd.dst_endpoint = msg->info.dst_endpoint,
                    .level = *(uint8_t*) f->extension_field_attribute_value_list,
                    .transition_time = msg->transition_time,
                };
                ESP_LOGI(TAG, "Recall scene %d for group %d: level=%d", msg->scene_id, msg->group_id, cmd_level.level);
                esp_zb_zcl_level_move_to_level_cmd_req(&cmd_level);
                break;
            case ESP_ZB_ZCL_CLUSTER_ID_COLOR_CONTROL:
                esp_zb_zcl_color_move_to_color_temperature_cmd_t cmd_temp = {
                    .address_mode = ESP_ZB_APS_ADDR_MODE_16_ENDP_PRESENT,
                    .zcl_basic_cmd.dst_addr_u.addr_short = esp_zb_get_short_address(),
                    .zcl_basic_cmd.src_endpoint = msg->info.dst_endpoint,
                    .zcl_basic_cmd.dst_endpoint = msg->info.dst_endpoint,
                    .color_temperature = *(uint16_t*) f->extension_field_attribute_value_list,
                    .transition_time = msg->transition_time,
                };
                ESP_LOGI(TAG, "Recall scene %d for group %d: temp=%d", msg->scene_id, msg->group_id, cmd_temp.color_temperature);
                esp_zb_zcl_color_move_to_color_temperature_cmd_req(&cmd_temp);
                break;
            default:
                ESP_LOGW(TAG, "Unknown field(s) to recall for endpoint %d, cluster %d", msg->info.dst_endpoint, f->cluster_id);
                break;
        }
        f = f->next;
    }
    // FIXME: For some reason, this approach with commands gets the device stuck
    // in an infinite loop, trying to set color. That is, until the next cmd comes.

    return err;
}
