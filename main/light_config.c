/*
 * ESP32 White Ambiance
 * Written in 2025 by Michal Jirků (wejn)
 *
 * This code is licensed under AGPL version 3.
 */
#include "esp_check.h"
#include "stdio.h"
#include "string.h"
#include "global_config.h"
#include "light_config.h"
#include "ha/esp_zigbee_ha_standard.h"
#include "esp_err.h"
#include "esp_app_desc.h"
#include "nvs.h"
#include "delayed_save.h"

#define LIGHT_CONFIG_NVS_NAMESPACE "light_config"
#define STARTUP_ONOFF_TOGGLE 2
#define STARTUP_ONOFF_PREVIOUS 0xff
#define STARTUP_LEVEL_PREVIOUS 0xff
#define STARTUP_TEMP_PREVIOUS 0xffff

static const char *TAG = "LIGHT_CONFIG";

static light_config_t light_config_rw = MY_LIGHT_CONFIG();
const light_config_t * const light_config = &light_config_rw;

#if(NVS_KEY_NAME_MAX_SIZE < 16)
#error We need at least 16 byte nvs keys
#endif
#define LCFV_AS_STRING(NAME) case LCFV_##NAME: return #NAME;
static const char *lc_flash_var_to_key(lc_flash_var_t var) {
    switch (var) {
        _LCFV_ITER(LCFV_AS_STRING)
        default: assert(!"Unknown flash variable");
    }
}
#undef LCFV_AS_STRING

esp_err_t light_config_erase_flash() {
    esp_err_t err;
    nvs_handle_t nvs_handle;

    err = nvs_open(LIGHT_CONFIG_NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "can't access flash to erase it: %s", esp_err_to_name(err));
        return err;
    }

    err = nvs_erase_all(nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "can't erase flash: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return err;
    }

    err = nvs_commit(nvs_handle);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "all flash erased");
    } else {
        ESP_LOGW(TAG, "can't erase flash (commit): %s", esp_err_to_name(err));
    }

    nvs_close(nvs_handle);

    return err;

}

esp_err_t light_config_persist_var(lc_flash_var_t key) {
    lc_flash_var_t vars[] = { key };
    return light_config_persist_vars(vars, 1);
}

esp_err_t light_config_persist_vars(lc_flash_var_t *vars, size_t num) {
    esp_err_t err;
    nvs_handle_t nvs_handle;

    err = nvs_open(LIGHT_CONFIG_NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "can't access flash to write settings: %s", esp_err_to_name(err));
        return err;
    }

    char k[NVS_KEY_NAME_MAX_SIZE];
    uint32_t value = 0;

    for (size_t i = 0; i < num; i++) {
        const char *ck = lc_flash_var_to_key(vars[i]);
        strncpy(k, ck, NVS_KEY_NAME_MAX_SIZE); // need to trim the key, sigh

#define LCFV_AS_LC_DEREF(NAME) case LCFV_##NAME: value = light_config->NAME; break;
        switch (vars[i]) {
            _LCFV_ITER(LCFV_AS_LC_DEREF)
        }
#undef LCFV_AS_LC_DEREF

        err = nvs_set_u32(nvs_handle, k, value);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "saved %s to flash: %lu", ck, value);
        } else {
            ESP_LOGW(TAG, "save of %s to flash err: %s", ck, esp_err_to_name(err));
            break;
        }
    }

    if (err == ESP_OK) {
        err = nvs_commit(nvs_handle);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "committed %d vars to flash", num);
        } else {
            ESP_LOGW(TAG, "commit of %d vars to flash err: %s", num, esp_err_to_name(err));
        }
    }

    nvs_close(nvs_handle);

    return err;
}

static esp_err_t lc_read_var_from_flash(nvs_handle_t nvs_handle, lc_flash_var_t key, uint32_t *val) {
    const char *ck = lc_flash_var_to_key(key);
    char k[NVS_KEY_NAME_MAX_SIZE];
    strncpy(k, ck, NVS_KEY_NAME_MAX_SIZE); // need to trim the key, sigh

    esp_err_t err = nvs_get_u32(nvs_handle, k, val);
    switch (err) {
        case ESP_OK:
            ESP_LOGI(TAG, "read %s from flash = %lu", ck, *val);
            break;
        case ESP_ERR_NVS_NOT_FOUND:
            ESP_LOGI(TAG, "read %s from flash = not found", ck);
            break;
        default:
            ESP_LOGW(TAG, "read %s from flash err: %s", ck, esp_err_to_name(err));
            break;
    }
    return err;
}

esp_err_t lc_restore_cfg_from_flash() {
    uint32_t val;
    esp_err_t err;
    nvs_handle_t nvs_handle;

    err = nvs_open(LIGHT_CONFIG_NVS_NAMESPACE, NVS_READWRITE, &nvs_handle); // readonly + empty flash → oops
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "can't access flash to restore settings: %s", esp_err_to_name(err));
        return err;
    }

    // onoff
    val = light_config_rw.startup_onoff;
    lc_read_var_from_flash(nvs_handle, LCFV_startup_onoff, &val);
    switch (val) {
        case 0: // off
            light_config_rw.onoff = 0;
            break;
        case 1: // on
            light_config_rw.onoff = 1;
            break;
        case STARTUP_ONOFF_TOGGLE:
            if (ESP_OK == lc_read_var_from_flash(nvs_handle, LCFV_onoff, &val)) {
                light_config_rw.onoff = !((bool) val);
            }
            break;
        case STARTUP_ONOFF_PREVIOUS:
            if (ESP_OK == lc_read_var_from_flash(nvs_handle, LCFV_onoff, &val)) {
                light_config_rw.onoff = ((bool) val);
            }
            break;
        default: // 0x3..0xfe, no action
            break;
    }

    // level
    if (ESP_OK == lc_read_var_from_flash(nvs_handle, LCFV_level_options, &val)) {
        light_config_rw.level_options = val;
    }

    val = light_config_rw.startup_level;
    lc_read_var_from_flash(nvs_handle, LCFV_startup_level, &val);
    switch (val) {
        case 0: // minimum
            light_config_rw.level = 1;
            break;
        case STARTUP_LEVEL_PREVIOUS:
            lc_read_var_from_flash(nvs_handle, LCFV_level, &val);
            light_config_rw.level = val;
            break;
        default:
            if (1 <= val && val <= 254) { // this level
                light_config_rw.level = val;
            }
    }

    // color
    if (ESP_OK == lc_read_var_from_flash(nvs_handle, LCFV_color_options, &val)) {
        light_config_rw.color_options = val;
    }

    val = light_config_rw.startup_temperature;
    lc_read_var_from_flash(nvs_handle, LCFV_startup_temperature, &val);
    if (val == STARTUP_TEMP_PREVIOUS) {
        if (ESP_OK == lc_read_var_from_flash(nvs_handle, LCFV_temperature, &val)) {
            light_config_rw.temperature = val;
        }
    } else {
        if (val <= 0xffef) { // this color
            light_config_rw.temperature = val;
        }
    }

    nvs_close(nvs_handle);

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

esp_zb_cluster_list_t *light_config_clusters_create() {
    esp_zb_cluster_list_t *cluster_list = esp_zb_zcl_cluster_list_create();

    // basic cluster
    esp_zb_basic_cluster_cfg_t basic_cfg = {
        .zcl_version = ESP_ZB_ZCL_BASIC_ZCL_VERSION_DEFAULT_VALUE,
        .power_source = light_config_rw.power_source,
    };
    esp_zb_attribute_list_t *basic_attr = esp_zb_basic_cluster_create(&basic_cfg);

    char basic_info[32];

    if (light_config_rw.manufacturer_name) {
        to_pascal_string(basic_info, light_config_rw.manufacturer_name, 32);
    } else {
        basic_info[0] = 0;
    }
    ADD_OR_WARN(esp_zb_basic_cluster_add_attr, basic_attr, ESP_ZB_ZCL_ATTR_BASIC_MANUFACTURER_NAME_ID, basic_info);

    if (light_config_rw.model_identifier) {
        to_pascal_string(basic_info, light_config_rw.model_identifier, 32);
    } else {
        basic_info[0] = 0;
    }
    ADD_OR_WARN(esp_zb_basic_cluster_add_attr, basic_attr, ESP_ZB_ZCL_ATTR_BASIC_MODEL_IDENTIFIER_ID, basic_info);

    if (light_config_rw.build_id) {
        to_pascal_string(basic_info, light_config_rw.build_id, 16);
    } else {
        const esp_app_desc_t *app_desc = esp_app_get_description();
        if (app_desc) {
            to_pascal_string(basic_info, app_desc->version, 16);
        } else {
            basic_info[0] = 0;
        }
    }
    ADD_OR_WARN(esp_zb_basic_cluster_add_attr, basic_attr, ESP_ZB_ZCL_ATTR_BASIC_SW_BUILD_ID, basic_info);

    if (light_config_rw.date_code) {
        to_pascal_string(basic_info, light_config_rw.date_code, 16);
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
        .on_off = light_config_rw.onoff,
    };
    esp_zb_attribute_list_t *on_off_attr = esp_zb_on_off_cluster_create(&on_off_cfg);

    uint16_t on_off_on_time = 0;
    bool on_off_global_scene_control = 1;
    uint16_t off_wait_time = 0;
    ADD_OR_WARN(esp_zb_on_off_cluster_add_attr, on_off_attr, ESP_ZB_ZCL_ATTR_ON_OFF_ON_TIME, &on_off_on_time);
    ADD_OR_WARN(esp_zb_on_off_cluster_add_attr, on_off_attr, ESP_ZB_ZCL_ATTR_ON_OFF_OFF_WAIT_TIME, &off_wait_time);
    ADD_OR_WARN(esp_zb_on_off_cluster_add_attr, on_off_attr, ESP_ZB_ZCL_ATTR_ON_OFF_GLOBAL_SCENE_CONTROL, &on_off_global_scene_control);
    ADD_OR_WARN(esp_zb_on_off_cluster_add_attr, on_off_attr, ESP_ZB_ZCL_ATTR_ON_OFF_START_UP_ON_OFF, &light_config_rw.startup_onoff);
    esp_zb_cluster_list_add_on_off_cluster(cluster_list, on_off_attr, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);

    // level cluster
    esp_zb_level_cluster_cfg_t level_cfg = {
        .current_level = light_config_rw.level,
    };
    esp_zb_attribute_list_t *level_attr = esp_zb_level_cluster_create(&level_cfg);
    ADD_OR_WARN(esp_zb_level_cluster_add_attr, level_attr, ESP_ZB_ZCL_ATTR_LEVEL_CONTROL_START_UP_CURRENT_LEVEL_ID, &light_config_rw.startup_level);
    ADD_OR_WARN(esp_zb_level_cluster_add_attr, level_attr, ESP_ZB_ZCL_ATTR_LEVEL_CONTROL_OPTIONS_ID, &light_config_rw.level_options);
    esp_zb_cluster_list_add_level_cluster(cluster_list, level_attr, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);

    // color cluster
    esp_zb_attribute_list_t *color_attr = esp_zb_zcl_attr_list_create(ESP_ZB_ZCL_CLUSTER_ID_COLOR_CONTROL);

    uint8_t color_mode = 0x02;
    uint16_t color_capabilities = 0x0010;
    ADD_OR_WARN(esp_zb_color_control_cluster_add_attr, color_attr, ESP_ZB_ZCL_ATTR_COLOR_CONTROL_OPTIONS_ID, &light_config_rw.color_options);
    ADD_OR_WARN(esp_zb_color_control_cluster_add_attr, color_attr, ESP_ZB_ZCL_ATTR_COLOR_CONTROL_COLOR_MODE_ID, &color_mode);
    ADD_OR_WARN(esp_zb_color_control_cluster_add_attr, color_attr, ESP_ZB_ZCL_ATTR_COLOR_CONTROL_ENHANCED_COLOR_MODE_ID, &color_mode);
    ADD_OR_WARN(esp_zb_color_control_cluster_add_attr, color_attr, ESP_ZB_ZCL_ATTR_COLOR_CONTROL_COLOR_CAPABILITIES_ID, &color_capabilities);
    ADD_OR_WARN(esp_zb_color_control_cluster_add_attr, color_attr, ESP_ZB_ZCL_ATTR_COLOR_CONTROL_COLOR_TEMPERATURE_ID, &light_config_rw.temperature);
    ADD_OR_WARN(esp_zb_color_control_cluster_add_attr, color_attr, ESP_ZB_ZCL_ATTR_COLOR_CONTROL_START_UP_COLOR_TEMPERATURE_MIREDS_ID, &light_config_rw.startup_temperature);
    ADD_OR_WARN(esp_zb_color_control_cluster_add_attr, color_attr, ESP_ZB_ZCL_ATTR_COLOR_CONTROL_COLOR_TEMP_PHYSICAL_MIN_MIREDS_ID, &light_config_rw.min_temperature);
    ADD_OR_WARN(esp_zb_color_control_cluster_add_attr, color_attr, ESP_ZB_ZCL_ATTR_COLOR_CONTROL_COLOR_TEMP_PHYSICAL_MAX_MIREDS_ID, &light_config_rw.max_temperature);
    ADD_OR_WARN(esp_zb_color_control_cluster_add_attr, color_attr, ESP_ZB_ZCL_ATTR_COLOR_CONTROL_COUPLE_COLOR_TEMP_TO_LEVEL_MIN_MIREDS_ID, &light_config_rw.couple_min_temperature);
    esp_zb_cluster_list_add_color_control_cluster(cluster_list, color_attr, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);

    return cluster_list;
}

esp_err_t light_config_initialize() {
    esp_err_t ret = ESP_OK;

    create_delayed_save_task();

    ret = lc_restore_cfg_from_flash();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "restore from flash failed: %s", esp_err_to_name(ret));
    }

    esp_err_t err = light_driver_update();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "light driver update failed: %s", esp_err_to_name(err));
        if (ret == ESP_OK) {
            ret = err;
        }
    }

    return ret;
}

esp_err_t light_config_update(lc_flash_var_t key, uint32_t val) {
    esp_err_t ret = ESP_OK;

    switch (key) {
        case LCFV_onoff:
            light_config_rw.onoff = val;
            if (light_config->startup_onoff == STARTUP_ONOFF_PREVIOUS ||
                    light_config->startup_onoff == STARTUP_ONOFF_TOGGLE) {
                trigger_delayed_save(DS_onoff);
            }
            ret = light_driver_update();
            break;
        case LCFV_startup_onoff:
            light_config_rw.startup_onoff = val;
            if (val == STARTUP_ONOFF_PREVIOUS || val == STARTUP_ONOFF_TOGGLE) {
                light_config_persist_var(LCFV_onoff);
            }
            light_config_persist_var(LCFV_startup_onoff);
            break;
        case LCFV_level_options:
            light_config_rw.level_options = val;
            light_config_persist_var(LCFV_level_options);
            break;
        case LCFV_level:
            light_config_rw.level = val;
            if (light_config->startup_level == STARTUP_LEVEL_PREVIOUS) {
                trigger_delayed_save(DS_level);
            }
            ret = light_driver_update();
            break;
        case LCFV_startup_level:
            light_config_rw.startup_level = val;
            if (val == STARTUP_LEVEL_PREVIOUS) {
                light_config_persist_var(LCFV_level);
            }
            light_config_persist_var(LCFV_startup_level);
            break;
        case LCFV_color_options:
            light_config_rw.color_options = val;
            light_config_persist_var(LCFV_color_options);
            break;
        case LCFV_temperature:
            light_config_rw.temperature = val;
            if (light_config->startup_temperature == STARTUP_TEMP_PREVIOUS) {
                trigger_delayed_save(DS_temperature);
            }
            ret = light_driver_update();
            break;
        case LCFV_startup_temperature:
            light_config_rw.startup_temperature = val;
            if (val == STARTUP_TEMP_PREVIOUS) {
                light_config_persist_var(LCFV_temperature);
            }
            light_config_persist_var(LCFV_startup_temperature);
            break;
    }

    return ret;
}
