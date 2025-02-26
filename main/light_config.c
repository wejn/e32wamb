/*
 * ESP32 White Ambiance
 * Written in 2025 by Michal Jirků (wejn)
 *
 * This code is licensed under AGPL version 3.
 */
#include "esp_check.h"
#include "stdio.h"
#include "string.h"
#include "e32wamb.h"
#include "light_config.h"
#include "ha/esp_zigbee_ha_standard.h"

static const char *TAG = "LIGHT_CONFIG";

#define MLFV_AS_STRING(NAME) case MLFV_##NAME: return "mlfv_"#NAME;
static const char *ml_flash_var_to_key(ml_flash_var_t var) {
    switch (var) {
        _MLFV_ITER(MLFV_AS_STRING)
        default: assert(!"Unknown flash variable");
    }
}
#undef MLFV_AS_STRING

esp_err_t my_light_save_var_to_flash(ml_flash_var_t key, uint32_t val) {
    ESP_LOGI(TAG, "save %s to flash: %lu", ml_flash_var_to_key(key), val);
    // FIXME: implement
    return ESP_OK;
}

static esp_err_t my_light_read_var_from_flash(ml_flash_var_t key, uint32_t *val) {
    ESP_LOGI(TAG, "read %s from flash", ml_flash_var_to_key(key));
    // FIXME: implement
    return ESP_OK;
}

esp_err_t my_light_restore_cfg_from_flash(my_light_cfg_t *light_cfg) {
    my_light_read_var_from_flash(MLFV_onoff, NULL);
    // FIXME: implement
    return ESP_OK;
}

esp_zb_cluster_list_t *my_light_clusters_create(my_light_cfg_t *light_cfg) {
    // FIXME: use the config & actually implement this? ;)
    esp_zb_color_dimmable_light_cfg_t dlc = DIMMABLE_LIGHT_CONFIG();
    esp_zb_cluster_list_t *cluster_list = esp_zb_color_dimmable_light_clusters_create(&dlc);

    basic_info_t info = {
        .manufacturer_name = light_cfg->manufacturer_name,
        .model_identifier = light_cfg->model_identifier,
        .date_code = light_cfg->date_code,
    };
    populate_basic_cluster_info(cluster_list, &info);

    // https://github.com/espressif/esp-zigbee-sdk/issues/457#issuecomment-2426128314
    uint16_t on_off_on_time = 0;
    bool on_off_global_scene_control = 0;
    esp_zb_attribute_list_t *onoff_attr_list =
        esp_zb_cluster_list_get_cluster(cluster_list, ESP_ZB_ZCL_CLUSTER_ID_ON_OFF, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);

    esp_zb_on_off_cluster_add_attr(onoff_attr_list, ESP_ZB_ZCL_ATTR_ON_OFF_ON_TIME, &on_off_on_time);
    esp_zb_on_off_cluster_add_attr(onoff_attr_list, ESP_ZB_ZCL_ATTR_ON_OFF_GLOBAL_SCENE_CONTROL,
            &on_off_global_scene_control);
    // .

    return cluster_list;
}
