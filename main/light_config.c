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
#include "esp_err.h"
#include "esp_app_desc.h"

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

/* Write pascal string (one byte length followed by chars from src) to dst.
 * The dst is supposed to be size chars long, src is C-style string.
 */
static void to_pascal_string(char *dst, const char *src, size_t size) {
    assert(size <= 255);
    memset(dst, 0, size);
    size_t actual_size = strlen(src) > (size - 1) ? size - 1 : strlen(src);
    dst[0] = actual_size & 0xff;
    memcpy(dst + 1, src, actual_size);
}

/* Add $prop with value $what to $where using $fun($where, $prop, $what);
 * if the function doesn't return ESP_OK, emit warning log message.
 */
#define ADD_OR_WARN(fun, where, prop, what) do { \
    esp_err_t err_rc_ = fun(where, prop, what); \
    if (err_rc_ != ESP_OK) { \
        ESP_LOGW(TAG, "Failed to add %s to %s: %d", #prop,  #where, err_rc_); \
    } \
} while(0)

esp_zb_cluster_list_t *my_light_clusters_create(my_light_cfg_t *light_cfg) {
    esp_zb_cluster_list_t *cluster_list = esp_zb_zcl_cluster_list_create();

    // basic cluster
    esp_zb_basic_cluster_cfg_t basic_cfg = {
        .zcl_version = ESP_ZB_ZCL_BASIC_ZCL_VERSION_DEFAULT_VALUE,
        .power_source = light_cfg->power_source,
    };
    esp_zb_attribute_list_t *basic_attr = esp_zb_basic_cluster_create(&basic_cfg);

    char basic_info[32];

    if (light_cfg->manufacturer_name) {
        to_pascal_string(basic_info, light_cfg->manufacturer_name, 32);
    } else {
        basic_info[0] = 0;
    }
    ADD_OR_WARN(esp_zb_basic_cluster_add_attr, basic_attr, ESP_ZB_ZCL_ATTR_BASIC_MANUFACTURER_NAME_ID, basic_info);

    if (light_cfg->model_identifier) {
        to_pascal_string(basic_info, light_cfg->model_identifier, 32);
    } else {
        basic_info[0] = 0;
    }
    ADD_OR_WARN(esp_zb_basic_cluster_add_attr, basic_attr, ESP_ZB_ZCL_ATTR_BASIC_MODEL_IDENTIFIER_ID, basic_info);

    if (light_cfg->build_id) {
        to_pascal_string(basic_info, light_cfg->build_id, 16);
    } else {
        const esp_app_desc_t *app_desc = esp_app_get_description();
        if (app_desc) {
            to_pascal_string(basic_info, app_desc->version, 16);
        } else {
            basic_info[0] = 0;
        }
    }
    ADD_OR_WARN(esp_zb_basic_cluster_add_attr, basic_attr, ESP_ZB_ZCL_ATTR_BASIC_SW_BUILD_ID, basic_info);

    if (light_cfg->date_code) {
        to_pascal_string(basic_info, light_cfg->date_code, 16);
    } else {
        basic_info[0] = 0;
    }
    ADD_OR_WARN(esp_zb_basic_cluster_add_attr, basic_attr, ESP_ZB_ZCL_ATTR_BASIC_DATE_CODE_ID, basic_info);

    esp_zb_cluster_list_add_basic_cluster(cluster_list, basic_attr, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);

    // identify cluster
    esp_zb_identify_cluster_cfg_t identify_cfg = {
        .identify_time = ESP_ZB_ZCL_IDENTIFY_IDENTIFY_TIME_DEFAULT_VALUE,
    };
    esp_zb_attribute_list_t *identify_attr = esp_zb_identify_cluster_create(&identify_cfg);
    esp_zb_cluster_list_add_identify_cluster(cluster_list, identify_attr, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);

    // groups cluster
    esp_zb_groups_cluster_cfg_t groups_cfg = {
        .groups_name_support_id = ESP_ZB_ZCL_GROUPS_NAME_SUPPORT_DEFAULT_VALUE,
    };
    esp_zb_attribute_list_t *groups_attr = esp_zb_groups_cluster_create(&groups_cfg);
    esp_zb_cluster_list_add_groups_cluster(cluster_list, groups_attr, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);

    // scenes cluster
    esp_zb_scenes_cluster_cfg_t scenes_cfg = {
        .scenes_count = ESP_ZB_ZCL_SCENES_SCENE_COUNT_DEFAULT_VALUE,
        .current_scene = ESP_ZB_ZCL_SCENES_CURRENT_SCENE_DEFAULT_VALUE,
        .current_group = ESP_ZB_ZCL_SCENES_CURRENT_GROUP_DEFAULT_VALUE,
        .scene_valid = ESP_ZB_ZCL_SCENES_SCENE_VALID_DEFAULT_VALUE,
        .name_support = ESP_ZB_ZCL_SCENES_NAME_SUPPORT_DEFAULT_VALUE,
    };
    esp_zb_attribute_list_t *scenes_attr = esp_zb_scenes_cluster_create(&scenes_cfg);
    esp_zb_cluster_list_add_scenes_cluster(cluster_list, scenes_attr, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);

    // onoff cluster
    esp_zb_on_off_cluster_cfg_t on_off_cfg = {
        .on_off = light_cfg->onoff,
    };
    esp_zb_attribute_list_t *on_off_attr = esp_zb_on_off_cluster_create(&on_off_cfg);

    uint16_t on_off_on_time = 0;
    bool on_off_global_scene_control = 1;
    uint16_t off_wait_time = 0;
    ADD_OR_WARN(esp_zb_on_off_cluster_add_attr, on_off_attr, ESP_ZB_ZCL_ATTR_ON_OFF_ON_TIME, &on_off_on_time);
    ADD_OR_WARN(esp_zb_on_off_cluster_add_attr, on_off_attr, ESP_ZB_ZCL_ATTR_ON_OFF_OFF_WAIT_TIME, &off_wait_time);
    ADD_OR_WARN(esp_zb_on_off_cluster_add_attr, on_off_attr, ESP_ZB_ZCL_ATTR_ON_OFF_GLOBAL_SCENE_CONTROL, &on_off_global_scene_control);
    ADD_OR_WARN(esp_zb_on_off_cluster_add_attr, on_off_attr, ESP_ZB_ZCL_ATTR_ON_OFF_START_UP_ON_OFF, &light_cfg->startup_onoff);
    esp_zb_cluster_list_add_on_off_cluster(cluster_list, on_off_attr, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);

    // level cluster
    esp_zb_level_cluster_cfg_t level_cfg = {
        .current_level = light_cfg->level,
    };
    esp_zb_attribute_list_t *level_attr = esp_zb_level_cluster_create(&level_cfg);
    ADD_OR_WARN(esp_zb_level_cluster_add_attr, level_attr, ESP_ZB_ZCL_ATTR_LEVEL_CONTROL_START_UP_CURRENT_LEVEL_ID, &light_cfg->startup_level);
    ADD_OR_WARN(esp_zb_level_cluster_add_attr, level_attr, ESP_ZB_ZCL_ATTR_LEVEL_CONTROL_OPTIONS_ID, &light_cfg->level_options);
    esp_zb_cluster_list_add_level_cluster(cluster_list, level_attr, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);

    // color cluster
    esp_zb_attribute_list_t *color_attr = esp_zb_zcl_attr_list_create(ESP_ZB_ZCL_CLUSTER_ID_COLOR_CONTROL);

    uint8_t color_mode = 0x02;
    uint16_t color_capabilities = 0x0010;
    ADD_OR_WARN(esp_zb_color_control_cluster_add_attr, color_attr, ESP_ZB_ZCL_ATTR_COLOR_CONTROL_OPTIONS_ID, &light_cfg->color_options);
    ADD_OR_WARN(esp_zb_color_control_cluster_add_attr, color_attr, ESP_ZB_ZCL_ATTR_COLOR_CONTROL_COLOR_MODE_ID, &color_mode);
    ADD_OR_WARN(esp_zb_color_control_cluster_add_attr, color_attr, ESP_ZB_ZCL_ATTR_COLOR_CONTROL_ENHANCED_COLOR_MODE_ID, &color_mode);
    ADD_OR_WARN(esp_zb_color_control_cluster_add_attr, color_attr, ESP_ZB_ZCL_ATTR_COLOR_CONTROL_COLOR_CAPABILITIES_ID, &color_capabilities);
    ADD_OR_WARN(esp_zb_color_control_cluster_add_attr, color_attr, ESP_ZB_ZCL_ATTR_COLOR_CONTROL_COLOR_TEMPERATURE_ID, &light_cfg->temp);
    ADD_OR_WARN(esp_zb_color_control_cluster_add_attr, color_attr, ESP_ZB_ZCL_ATTR_COLOR_CONTROL_START_UP_COLOR_TEMPERATURE_MIREDS_ID, &light_cfg->startup_temp);
    ADD_OR_WARN(esp_zb_color_control_cluster_add_attr, color_attr, ESP_ZB_ZCL_ATTR_COLOR_CONTROL_COLOR_TEMP_PHYSICAL_MIN_MIREDS_ID, &light_cfg->min_temp);
    ADD_OR_WARN(esp_zb_color_control_cluster_add_attr, color_attr, ESP_ZB_ZCL_ATTR_COLOR_CONTROL_COLOR_TEMP_PHYSICAL_MAX_MIREDS_ID, &light_cfg->max_temp);
    ADD_OR_WARN(esp_zb_color_control_cluster_add_attr, color_attr, ESP_ZB_ZCL_ATTR_COLOR_CONTROL_COUPLE_COLOR_TEMP_TO_LEVEL_MIN_MIREDS_ID, &light_cfg->couple_min_temp);
    esp_zb_cluster_list_add_color_control_cluster(cluster_list, color_attr, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);

    return cluster_list;
}
