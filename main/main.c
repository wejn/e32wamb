/*
 * ESP32 White Ambiance
 * Written in 2025 by Michal Jirků (wejn)
 *
 * This code is licensed under AGPL version 3.
 * Originally forked from https://github.com/wejn/esp32-huello-world.
 */
#include "main.h"
#include "esp_check.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ha/esp_zigbee_ha_standard.h"
#include "light_config.h"
#include "delayed_save.h"

#if !defined CONFIG_ZB_ZCZR
#error Define ZB_ZCZR in idf.py menuconfig to compile light (Router) source code.
#endif

#if defined HAVE_TRUST_CENTER_KEY
#include "trust_center_key.h"
#else
#warning I do not have proper trust_center_key.h present, this will NOT link to Hue bridge.
// this is not the correct key, replace it with the proper one in trust_center_key.h
#define TRUST_CENTER_KEY { \
    0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, \
    0x88, 0x99, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF \
}
#endif

static const char *TAG = "E32WAMB";

/********************* Define functions **************************/
static esp_err_t deferred_driver_init(void)
{
    // XXX: light_driver_init(LIGHT_DEFAULT_OFF);
    return ESP_OK;
}

static void bdb_start_top_level_commissioning_cb(uint8_t mode_mask)
{
    ESP_RETURN_ON_FALSE(esp_zb_bdb_start_top_level_commissioning(mode_mask) == ESP_OK, , TAG, "Failed to start Zigbee commissioning");
}

void esp_zb_app_signal_handler(esp_zb_app_signal_t *signal_struct)
{
    uint32_t *p_sg_p = signal_struct->p_app_signal;
    esp_err_t err_status = signal_struct->esp_err_status;
    esp_zb_app_signal_type_t sig_type = *p_sg_p;
    switch (sig_type) {
    case ESP_ZB_ZDO_SIGNAL_SKIP_STARTUP:
        ESP_LOGI(TAG, "Initialize Zigbee stack");
        esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_INITIALIZATION);
        break;
    case ESP_ZB_BDB_SIGNAL_DEVICE_FIRST_START:
    case ESP_ZB_BDB_SIGNAL_DEVICE_REBOOT:
        if (err_status == ESP_OK) {
            ESP_LOGI(TAG, "Deferred driver initialization %s", deferred_driver_init() ? "failed" : "successful");
            ESP_LOGI(TAG, "Device started up in %s factory-reset mode", esp_zb_bdb_is_factory_new() ? "" : "non");
            if (esp_zb_bdb_is_factory_new()) {
                ESP_LOGI(TAG, "Start network steering");
                esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_NETWORK_STEERING);
            } else {
                ESP_LOGI(TAG, "Device rebooted");
            }
        } else {
            ESP_LOGW(TAG, "Failed to initialize Zigbee stack (status: %s)", esp_err_to_name(err_status));
        }
        break;
    case ESP_ZB_BDB_SIGNAL_STEERING:
        if (err_status == ESP_OK) {
            esp_zb_ieee_addr_t extended_pan_id;
            esp_zb_get_extended_pan_id(extended_pan_id);
            ESP_LOGI(TAG, "Joined network successfully (Extended PAN ID: %02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x, PAN ID: 0x%04hx, Channel:%d, Short Address: 0x%04hx)",
                     extended_pan_id[7], extended_pan_id[6], extended_pan_id[5], extended_pan_id[4],
                     extended_pan_id[3], extended_pan_id[2], extended_pan_id[1], extended_pan_id[0],
                     esp_zb_get_pan_id(), esp_zb_get_current_channel(), esp_zb_get_short_address());
        } else {
            // FIXME: How about saying something like -- network not joined yet
            ESP_LOGI(TAG, "Network steering was not successful (status: %s)", esp_err_to_name(err_status));
            esp_zb_scheduler_alarm((esp_zb_callback_t)bdb_start_top_level_commissioning_cb, ESP_ZB_BDB_MODE_NETWORK_STEERING, 1000);
        }
        break;
    case ESP_ZB_NWK_SIGNAL_PERMIT_JOIN_STATUS:
        if (err_status == ESP_OK) {
            if (*(uint8_t *)esp_zb_app_signal_get_params(p_sg_p)) {
                ESP_LOGI(TAG, "Network(0x%04hx) is open for %d seconds", esp_zb_get_pan_id(), *(uint8_t *)esp_zb_app_signal_get_params(p_sg_p));
            } else {
                ESP_LOGW(TAG, "Network(0x%04hx) closed, devices joining not allowed.", esp_zb_get_pan_id());
            }
        }
        break;
    case ESP_ZB_ZDO_SIGNAL_LEAVE:
        // https://github.com/espressif/esp-zigbee-sdk/issues/66#issuecomment-1667314481
        esp_zb_zdo_signal_leave_params_t *leave_params = (esp_zb_zdo_signal_leave_params_t *)esp_zb_app_signal_get_params(p_sg_p);
        if (leave_params) {
            if (leave_params->leave_type == ESP_ZB_NWK_LEAVE_TYPE_RESET) {
                ESP_LOGI(TAG, "ZDO leave: with reset, status: %s", esp_err_to_name(err_status));
                esp_zb_nvram_erase_at_start(true); // erase previous network information.
                my_light_erase_flash(); // erase all config from flash
                esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_NETWORK_STEERING); // steering a new network.
            } else {
                ESP_LOGI(TAG, "ZDO leave: leave_type: %d, status: %s", leave_params->leave_type, esp_err_to_name(err_status));
            }
        } else {
            ESP_LOGI(TAG, "ZDO leave: (no params), status: %s", esp_err_to_name(err_status));
        }
        break;
    case ESP_ZB_NLME_STATUS_INDICATION:
        ESP_LOGI(TAG, "Network status: 0x%x", *(uint8_t *)esp_zb_app_signal_get_params(p_sg_p));
        // no-op, this is likely something like "alive" heartbeat
        break;
    case ESP_ZB_NWK_SIGNAL_NO_ACTIVE_LINKS_LEFT:
        ESP_LOGI(TAG, "No longer connected to coordinator!");
        break;
    case ESP_ZB_ZDO_SIGNAL_DEVICE_ANNCE:
        ESP_LOGI(TAG, "Rejoined network.");
        break;
    default:
        ESP_LOGI(TAG, "ZDO signal: %s (0x%x), status: %s", esp_zb_zdo_signal_to_string(sig_type), sig_type, esp_err_to_name(err_status));
        break;
    }
}

#define DATA_OR(type, deflt) (message->attribute.data.value ? *(type *)message->attribute.data.value : deflt)
#define WARN_UNKNOWN(cluster) \
    ESP_LOGW(TAG, "%s attr: unknown attribute(0x%x), type(0x%x), data size(%d)", cluster, message->attribute.id, message->attribute.data.type, message->attribute.data.size)
#define IF_ATTR_IS_TYPE(cluster, attr, attr_type) \
    if (message->attribute.data.type != attr_type) { \
        ESP_LOGW(TAG, "%s: unexpected type for %s: expected %s, got type(0x%x) with size(%d)", cluster, attr, #attr_type, message->attribute.data.type, message->attribute.data.size); \
    } else

static esp_err_t onoff_attribute_handler(const esp_zb_zcl_set_attr_value_message_t *message) {
    bool light_state = 0;
    uint16_t on_time = 0;
    uint16_t off_wait_time = 0;
    uint8_t startup_onoff = 0;

    switch (message->attribute.id) {
        case ESP_ZB_ZCL_ATTR_ON_OFF_ON_OFF_ID:
            IF_ATTR_IS_TYPE("onoff", "onoff", ESP_ZB_ZCL_ATTR_TYPE_BOOL) {
                light_state = DATA_OR(bool, light_state);
                // FIXME: implement
                // XXX: light_driver_set_power(light_state);
                ESP_LOGI(TAG, "Light sets to %s", light_state ? "On" : "Off");
                trigger_delayed_save(DS_onoff, light_state);
            }
            break;
        case ESP_ZB_ZCL_ATTR_ON_OFF_ON_TIME: // uint16
            IF_ATTR_IS_TYPE("onoff", "on_time", ESP_ZB_ZCL_ATTR_TYPE_U16) {
                on_time = DATA_OR(uint16_t, on_time);
                ESP_LOGI(TAG, "On time: %u", on_time);
                // no-op, handled internally by zboss
            }
            break;
        case ESP_ZB_ZCL_ATTR_ON_OFF_OFF_WAIT_TIME: // uint16
            IF_ATTR_IS_TYPE("onoff", "off_wait_time", ESP_ZB_ZCL_ATTR_TYPE_U16) {
                off_wait_time = DATA_OR(uint16_t, off_wait_time);
                ESP_LOGI(TAG, "Off wait time: %u", off_wait_time);
                // no-op, handled internally by zboss
            }
            break;
        case ESP_ZB_ZCL_ATTR_ON_OFF_START_UP_ON_OFF: // enum8
            IF_ATTR_IS_TYPE("onoff", "startup_onoff", ESP_ZB_ZCL_ATTR_TYPE_8BIT_ENUM) {
                startup_onoff = DATA_OR(uint8_t, startup_onoff);
                ESP_LOGI(TAG, "Startup onoff: %u", startup_onoff);
                my_light_save_var_to_flash(MLFV_startup_onoff, startup_onoff);
            }
            break;
        default:
            WARN_UNKNOWN("on/off");
    }
    return ESP_OK;
}

static esp_err_t level_attribute_handler(const esp_zb_zcl_set_attr_value_message_t *message) {
    uint8_t level = 0;
    uint8_t startup_level = 0;
    uint8_t options = 0;

    switch (message->attribute.id) {
        case ESP_ZB_ZCL_ATTR_LEVEL_CONTROL_CURRENT_LEVEL_ID:
            IF_ATTR_IS_TYPE("level", "current_level", ESP_ZB_ZCL_ATTR_TYPE_U8) {
                level = DATA_OR(uint8_t, level);
                // FIXME: implement
                // XXX: light_driver_set_level((uint8_t)level);
                ESP_LOGI(TAG, "Light level changes to %u", level);
                trigger_delayed_save(DS_level, level);
            }
            break;
        case ESP_ZB_ZCL_ATTR_LEVEL_CONTROL_START_UP_CURRENT_LEVEL_ID: // uint8
            IF_ATTR_IS_TYPE("level", "startup_level", ESP_ZB_ZCL_ATTR_TYPE_U8) {
                startup_level = DATA_OR(uint8_t, startup_level);
                ESP_LOGI(TAG, "Startup level: %u", startup_level);
                my_light_save_var_to_flash(MLFV_startup_level, startup_level);
            }
            break;
        case ESP_ZB_ZCL_ATTR_LEVEL_CONTROL_OPTIONS_ID: // map8
            IF_ATTR_IS_TYPE("level", "options", ESP_ZB_ZCL_ATTR_TYPE_8BITMAP) {
                options = DATA_OR(uint8_t, options);
                ESP_LOGI(TAG, "Level options: %x", options);
                my_light_save_var_to_flash(MLFV_level_options, options);
            }
            break;
        default:
            WARN_UNKNOWN("level");
    }
    return ESP_OK;
}

static esp_err_t color_attribute_handler(const esp_zb_zcl_set_attr_value_message_t *message) {
    uint16_t temperature = 0;
    uint16_t startup_temperature = 0;
    uint8_t options = 0;

    switch (message->attribute.id) {
        case ESP_ZB_ZCL_ATTR_COLOR_CONTROL_COLOR_TEMPERATURE_ID:
            IF_ATTR_IS_TYPE("color", "temperature", ESP_ZB_ZCL_ATTR_TYPE_U16) {
                temperature = DATA_OR(uint16_t, temperature);
                // FIXME: implement
                // XXX: light_driver_set_color_xy(light_color_x, light_color_y);
                ESP_LOGI(TAG, "Light temperature change to %u", temperature);
                trigger_delayed_save(DS_temperature, temperature);
            } 
            break;
        case ESP_ZB_ZCL_ATTR_COLOR_CONTROL_OPTIONS_ID: // map8
            IF_ATTR_IS_TYPE("color", "options", ESP_ZB_ZCL_ATTR_TYPE_8BITMAP) {
                options = DATA_OR(uint8_t, options);
                ESP_LOGI(TAG, "Color options: %x", options);
                my_light_save_var_to_flash(MLFV_color_options, options);
            }
            break;
        case ESP_ZB_ZCL_ATTR_COLOR_CONTROL_START_UP_COLOR_TEMPERATURE_MIREDS_ID: // uint16
            IF_ATTR_IS_TYPE("level", "startup_temperature", ESP_ZB_ZCL_ATTR_TYPE_U16) {
                startup_temperature = DATA_OR(uint16_t, startup_temperature);
                ESP_LOGI(TAG, "Startup temperature: %u", startup_temperature);
                my_light_save_var_to_flash(MLFV_startup_temp, startup_temperature);
            }
            break;
            break;
        default:
            WARN_UNKNOWN("color");
    }
    return ESP_OK;
}

static esp_err_t zb_attribute_handler(const esp_zb_zcl_set_attr_value_message_t *message)
{
    ESP_RETURN_ON_FALSE(message, ESP_FAIL, TAG, "Empty message");
    ESP_RETURN_ON_FALSE(message->info.status == ESP_ZB_ZCL_STATUS_SUCCESS, ESP_ERR_INVALID_ARG, TAG, "Received message: error status(%d)", message->info.status);
    // ESP_LOGI(TAG, "Received message: endpoint(%d), cluster(0x%x), attribute(0x%x), data size(%d)", message->info.dst_endpoint, message->info.cluster, message->attribute.id, message->attribute.data.size);

    if (message->info.dst_endpoint != MY_LIGHT_ENDPOINT) {
        ESP_LOGW(TAG, "Received message for unconfigured endpoint(%d): cluster(0x%x), attribute(0x%x), data size(%d)", message->info.dst_endpoint, message->info.cluster, message->attribute.id, message->attribute.data.size);
        return ESP_ERR_INVALID_ARG;
    }

    // basic, identify, groups, scenes, onoff, level, color
    switch (message->info.cluster) {
        case ESP_ZB_ZCL_CLUSTER_ID_ON_OFF:
            return onoff_attribute_handler(message);
        case ESP_ZB_ZCL_CLUSTER_ID_LEVEL_CONTROL:
            return level_attribute_handler(message);
        case ESP_ZB_ZCL_CLUSTER_ID_COLOR_CONTROL:
            return color_attribute_handler(message);
        default:
            ESP_LOGW(TAG, "Unknown attribute: cluster(0x%x), attribute(0x%x)", message->info.cluster, message->attribute.id);
    }
    return ESP_OK;
}

static esp_err_t zb_action_handler(esp_zb_core_action_callback_id_t callback_id, const void *message)
{
    esp_err_t ret = ESP_OK;
    switch (callback_id) {
        case ESP_ZB_CORE_SET_ATTR_VALUE_CB_ID:
            ret = zb_attribute_handler((esp_zb_zcl_set_attr_value_message_t *)message);
            break;
        case ESP_ZB_CORE_SCENES_STORE_SCENE_CB_ID:
            ESP_LOGW(TAG, "Got store scene callback");
            // FIXME: implement
            break;
        case ESP_ZB_CORE_SCENES_RECALL_SCENE_CB_ID:
            ESP_LOGW(TAG, "Got recall scene callback");
            // FIXME: implement
            break;
        // FIXME: also triggering: ESP_ZB_CORE_IDENTIFY_EFFECT_CB_ID
        default:
            ESP_LOGW(TAG, "Receive Zigbee action(0x%x) callback", callback_id);
            break;
    }
    return ret;
}

static void esp_zb_task(void *pvParameters)
{
    /* initialize Zigbee stack */
    esp_zb_cfg_t zb_nwk_cfg = ESP_ZB_ZR_CONFIG();
    esp_zb_init(&zb_nwk_cfg);

    // allow joining the Philips Hue network(s)
    esp_zb_enable_joining_to_distributed(true);
    uint8_t secret_zll_trust_center_key[] = TRUST_CENTER_KEY;
    esp_zb_secur_TC_standard_distributed_key_set(secret_zll_trust_center_key);

    esp_zb_ep_list_t *light_ep = esp_zb_ep_list_create();

    my_light_cfg_t mlc = MY_LIGHT_CONFIG();
    my_light_restore_cfg_from_flash(&mlc); // FIXME: error checking?
    esp_zb_cluster_list_t *cluster_list = my_light_clusters_create(&mlc);

    esp_zb_endpoint_config_t endpoint_config = MY_EP_CONFIG();
    esp_zb_ep_list_add_ep(light_ep, cluster_list, endpoint_config);

    esp_zb_device_register(light_ep);
    esp_zb_core_action_handler_register(zb_action_handler);
    esp_zb_set_primary_network_channel_set(ESP_ZB_TRANSCEIVER_ALL_CHANNELS_MASK);
    ESP_ERROR_CHECK(esp_zb_start(false));
    esp_zb_stack_main_loop();
}

void app_main(void)
{
    esp_zb_platform_config_t config = {
        .radio_config = ESP_ZB_DEFAULT_RADIO_CONFIG(),
        .host_config = ESP_ZB_DEFAULT_HOST_CONFIG(),
    };
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_zb_platform_config(&config));
    create_delayed_save_task();
    xTaskCreate(esp_zb_task, "Zigbee_main", 4096, NULL, 5, NULL);
}
