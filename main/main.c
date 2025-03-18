/*
 * ESP32 White Ambiance
 * Copyright © 2025 Michal Jirků (wejn)
 *
 * This code is licensed under GPL version 3.
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
#include "scenes.h"
#include "light_driver.h"
#include "status_indicator.h"
#include "reset_button.h"
#include "zboss_api.h"
#include "esp_timer.h"

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

uint64_t light_endpoint_last_queried_time = 0;

static const char *TAG = "MAIN";

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
        ESP_LOGI(TAG, "Initializing Zigbee stack");
        esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_INITIALIZATION);
        break;
    case ESP_ZB_BDB_SIGNAL_DEVICE_FIRST_START:
    case ESP_ZB_BDB_SIGNAL_DEVICE_REBOOT:
        if (err_status == ESP_OK) {
            if (esp_zb_bdb_is_factory_new()) {
                ESP_LOGI(TAG, "Start commissioning (network steering)");
                esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_NETWORK_STEERING);
            } else {
                ESP_LOGI(TAG, "Device rebooted, joining network 0x%04hx as 0x%04hx", esp_zb_get_pan_id(), esp_zb_get_short_address());
            }
        } else {
            ESP_LOGW(TAG, "Failed to initialize Zigbee stack; status: %s", esp_err_to_name(err_status));
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
            ESP_LOGI(TAG, "No network joined yet (status: %s)", esp_err_to_name(err_status));
            esp_zb_scheduler_alarm((esp_zb_callback_t)bdb_start_top_level_commissioning_cb, ESP_ZB_BDB_MODE_NETWORK_STEERING, 1000);
        }
        break;
    case ESP_ZB_NWK_SIGNAL_PERMIT_JOIN_STATUS:
        if (err_status == ESP_OK) {
            if (*(uint8_t *)esp_zb_app_signal_get_params(p_sg_p)) {
                ESP_LOGI(TAG, "Network 0x%04hx is open for %ds", esp_zb_get_pan_id(), *(uint8_t *)esp_zb_app_signal_get_params(p_sg_p));
            } else {
                ESP_LOGI(TAG, "Network 0x%04hx is closed, devices joining not allowed", esp_zb_get_pan_id());
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
                light_config_erase_flash(); // erase all config from flash
                esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_NETWORK_STEERING); // steering a new network.
            } else {
                ESP_LOGI(TAG, "ZDO leave: leave_type: %d, status: %s", leave_params->leave_type, esp_err_to_name(err_status));
            }
        } else {
            ESP_LOGI(TAG, "ZDO leave: (no params), status: %s", esp_err_to_name(err_status));
        }
        break;
    case ESP_ZB_NLME_STATUS_INDICATION:
        // esp_zb_zdo_signal_nwk_status_indication_params_t *ns = (esp_zb_zdo_signal_nwk_status_indication_params_t *) esp_zb_app_signal_get_params(p_sg_p);
        // ESP_LOGI(TAG, "Network status: 0x%x for address: 0x%04hx (uci: 0x%x)", ns->status, ns->network_addr, ns->unknown_command_id);
        // Informative messages about given device on network; see https://docs.espressif.com/projects/esp-zigbee-sdk/en/latest/esp32/api-reference/nwk/esp_zigbee_nwk.html#_CPPv427esp_zb_nwk_command_status_t
        // No-op.
        break;
    case ESP_ZB_NWK_SIGNAL_NO_ACTIVE_LINKS_LEFT:
        // This only means we're alone (no other nodes), but it can't be used to detect "offline" status;
        // i.e. a situation without coord, but with other routers/end devices present.
        // No-op.
        break;
    case ESP_ZB_ZDO_SIGNAL_DEVICE_ANNCE:
        // esp_zb_zdo_signal_device_annce_params_t *da = (esp_zb_zdo_signal_device_annce_params_t *) esp_zb_app_signal_get_params(p_sg_p);
        // ESP_LOGI(TAG, "Device 0x%04hx with caps 0x%x (re-)joined network.", da->device_short_addr, da->capability);
        // No-op. We don't need to know about newly joining devices.
        break;
    case ESP_ZB_ZDO_SIGNAL_PRODUCTION_CONFIG_READY:
        // No-op. Loaded config (congrats!)
        break;
    default:
        ESP_LOGI(TAG, "Unhandled ZDO signal: %s (0x%x), status: %s", esp_zb_zdo_signal_to_string(sig_type), sig_type, esp_err_to_name(err_status));
        break;
    }
}

#define WARN_UNKNOWN(cluster) \
    ESP_LOGW(TAG, "%s attr: unknown attribute: 0x%x, type: 0x%x, size: %d", cluster, message->attribute.id, message->attribute.data.type, message->attribute.data.size)
#define IF_ATTR_IS_TYPE_AND_PRESENT(cluster, attr, attr_type) \
    if (message->attribute.data.type != attr_type) { \
        ESP_LOGW(TAG, "%s: unexpected type for %s: expected %s, got type: 0x%x with size: %d", cluster, attr, #attr_type, message->attribute.data.type, message->attribute.data.size); \
    } else if (! message->attribute.data.value) { \
        ESP_LOGW(TAG, "%s: unexpectedly no value for %s", cluster, attr); \
    } else


static esp_err_t onoff_attribute_handler(const esp_zb_zcl_set_attr_value_message_t *message) {
    switch (message->attribute.id) {
        case ESP_ZB_ZCL_ATTR_ON_OFF_ON_OFF_ID:
            IF_ATTR_IS_TYPE_AND_PRESENT("onoff", "onoff", ESP_ZB_ZCL_ATTR_TYPE_BOOL) {
                light_config_update(LCFV_onoff, *(bool *)message->attribute.data.value);
                ESP_LOGI(TAG, "Light turns %s", light_config->onoff ? "on" : "off");
            }
            break;
        case ESP_ZB_ZCL_ATTR_ON_OFF_ON_TIME: // uint16
            IF_ATTR_IS_TYPE_AND_PRESENT("onoff", "on_time", ESP_ZB_ZCL_ATTR_TYPE_U16) {
                // no-op, handled internally by zboss
                ESP_LOGI(TAG, "On time: %u", *(uint16_t *)message->attribute.data.value);
            }
            break;
        case ESP_ZB_ZCL_ATTR_ON_OFF_OFF_WAIT_TIME: // uint16
            IF_ATTR_IS_TYPE_AND_PRESENT("onoff", "off_wait_time", ESP_ZB_ZCL_ATTR_TYPE_U16) {
                // no-op, handled internally by zboss
                ESP_LOGI(TAG, "Off wait time: %u", *(uint16_t *)message->attribute.data.value);
            }
            break;
        case ESP_ZB_ZCL_ATTR_ON_OFF_START_UP_ON_OFF: // enum8
            IF_ATTR_IS_TYPE_AND_PRESENT("onoff", "startup_onoff", ESP_ZB_ZCL_ATTR_TYPE_8BIT_ENUM) {
                light_config_update(LCFV_startup_onoff, *(uint8_t *)message->attribute.data.value);
                ESP_LOGI(TAG, "Startup onoff: %u", light_config->startup_onoff);
            }
            break;
        default:
            WARN_UNKNOWN("on/off");
    }
    return ESP_OK;
}

static esp_err_t level_attribute_handler(const esp_zb_zcl_set_attr_value_message_t *message) {
    switch (message->attribute.id) {
        case ESP_ZB_ZCL_ATTR_LEVEL_CONTROL_CURRENT_LEVEL_ID:
            IF_ATTR_IS_TYPE_AND_PRESENT("level", "current_level", ESP_ZB_ZCL_ATTR_TYPE_U8) {
                light_config_update(LCFV_level, *(uint8_t *)message->attribute.data.value);
                ESP_LOGI(TAG, "Light level changes to %u", light_config->level);
            }
            break;
        case ESP_ZB_ZCL_ATTR_LEVEL_CONTROL_START_UP_CURRENT_LEVEL_ID: // uint8
            IF_ATTR_IS_TYPE_AND_PRESENT("level", "startup_level", ESP_ZB_ZCL_ATTR_TYPE_U8) {
                light_config_update(LCFV_startup_level, *(uint8_t *)message->attribute.data.value);
                ESP_LOGI(TAG, "Startup level: %u", light_config->startup_level);
            }
            break;
        case ESP_ZB_ZCL_ATTR_LEVEL_CONTROL_OPTIONS_ID: // map8
            IF_ATTR_IS_TYPE_AND_PRESENT("level", "options", ESP_ZB_ZCL_ATTR_TYPE_8BITMAP) {
                light_config_update(LCFV_level_options, *(uint8_t *)message->attribute.data.value);
                ESP_LOGI(TAG, "Level options: %x", light_config->level_options);
            }
            break;
        default:
            WARN_UNKNOWN("level");
    }
    return ESP_OK;
}

static esp_err_t color_attribute_handler(const esp_zb_zcl_set_attr_value_message_t *message) {
    switch (message->attribute.id) {
        case ESP_ZB_ZCL_ATTR_COLOR_CONTROL_COLOR_TEMPERATURE_ID:
            IF_ATTR_IS_TYPE_AND_PRESENT("color", "temperature", ESP_ZB_ZCL_ATTR_TYPE_U16) {
                light_config_update(LCFV_temperature, *(uint16_t *)message->attribute.data.value);
                ESP_LOGI(TAG, "Light temperature change to %u", light_config->temperature);
            }
            break;
        case ESP_ZB_ZCL_ATTR_COLOR_CONTROL_OPTIONS_ID: // map8
            IF_ATTR_IS_TYPE_AND_PRESENT("color", "options", ESP_ZB_ZCL_ATTR_TYPE_8BITMAP) {
                light_config_update(LCFV_color_options, *(uint8_t *)message->attribute.data.value);
                ESP_LOGI(TAG, "Color options: %x", light_config->color_options);
            }
            break;
        case ESP_ZB_ZCL_ATTR_COLOR_CONTROL_START_UP_COLOR_TEMPERATURE_MIREDS_ID: // uint16
            IF_ATTR_IS_TYPE_AND_PRESENT("level", "startup_temperature", ESP_ZB_ZCL_ATTR_TYPE_U16) {
                light_config_update(LCFV_startup_temperature, *(uint16_t *)message->attribute.data.value);
                ESP_LOGI(TAG, "Startup temperature: %u", light_config->startup_temperature);
            }
            break;
        default:
            WARN_UNKNOWN("color");
    }
    return ESP_OK;
}

static esp_err_t zb_attribute_handler(const esp_zb_zcl_set_attr_value_message_t *message)
{
    ESP_RETURN_ON_FALSE(message, ESP_FAIL, TAG, "Empty message");
    ESP_RETURN_ON_FALSE(message->info.status == ESP_ZB_ZCL_STATUS_SUCCESS, ESP_ERR_INVALID_ARG, TAG, "Received message: error status: %d", message->info.status);
    // ESP_LOGI(TAG, "Received message: endpoint: %d, cluster: 0x%x, attribute: 0x%x, size: %d", message->info.dst_endpoint, message->info.cluster, message->attribute.id, message->attribute.data.size);

    if (message->info.dst_endpoint != MY_LIGHT_ENDPOINT) {
        ESP_LOGW(TAG, "Received message for unconfigured endpoint; ep: %d, cluster: 0x%x, attribute: 0x%x, size: %d", message->info.dst_endpoint, message->info.cluster, message->attribute.id, message->attribute.data.size);
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
            ESP_LOGW(TAG, "Unknown attribute: cluster: 0x%x, attribute: 0x%x", message->info.cluster, message->attribute.id);
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
            ret = store_scene((esp_zb_zcl_store_scene_message_t*) message);
            break;
        case ESP_ZB_CORE_SCENES_RECALL_SCENE_CB_ID:
            ret = recall_scene((esp_zb_zcl_recall_scene_message_t*) message);
            break;
        case ESP_ZB_CORE_CMD_DEFAULT_RESP_CB_ID:
            // XXX: Hue Bridge on restart sometimes asks for onoff attribute
            // value, and then responds to the reply with 0x82 (unsupported)
            esp_zb_zcl_cmd_default_resp_message_t *cdr = (esp_zb_zcl_cmd_default_resp_message_t *) message;
            esp_zb_zcl_cmd_info_t *i = &(cdr->info);
            ESP_LOGW(TAG, "CMD default resp; cmd: 0x%x, status: 0x%x, info: [src: 0x%04hx, dst: 0x%04hx, se: %d, de: %d, cl: 0x%04hx, prof: 0x%04hx]", cdr->resp_to_cmd, cdr->status_code, i->src_address.u.short_addr, i->dst_address, i->src_endpoint, i->dst_endpoint, i->cluster, i->profile);
            // XXX: ^^ this will show bullshit src address IF not a short one.
            break;
        // FIXME: also triggering: ESP_ZB_CORE_IDENTIFY_EFFECT_CB_ID
        default:
            ESP_LOGW(TAG, "Received unhandled action callback: 0x%x", callback_id);
            break;
    }
    return ret;
}

bool zb_raw_command_handler(uint8_t bufid) {
    // Hello https://github.com/espressif/esp-zigbee-sdk/issues/597

    // uint8_t buf[zb_buf_len(bufid)];
    zb_zcl_parsed_hdr_t *cmd_info = ZB_BUF_GET_PARAM(bufid, zb_zcl_parsed_hdr_t);
    // memcpy(buf, zb_buf_begin(bufid), sizeof(buf));

    if (cmd_info->addr_data.common_data.dst_endpoint == MY_LIGHT_ENDPOINT && cmd_info->cmd_id == ZB_ZCL_CMD_READ_ATTRIB) {
        /*
           zb_zcl_read_attr_req_t *ra_req = (zb_zcl_read_attr_req_t *)buf;
           ESP_LOGI("READ_ATTR", "From: 0x%04x, Endpoint: 0x%d, ClusterID: 0x%04x, AttrID: 0x%04x",
           cmd_info->addr_data.common_data.source.u.short_addr,
           cmd_info->addr_data.common_data.dst_endpoint,
           cmd_info->cluster_id,
         *ra_req->attr_id); */
        light_endpoint_last_queried_time = esp_timer_get_time();
    }

    return false;
}

static void esp_zb_task(void *pvParameters)
{
    // initialize Zigbee stack
    esp_zb_cfg_t zb_nwk_cfg = ESP_ZB_ZR_CONFIG();
    esp_zb_init(&zb_nwk_cfg);

    // allow joining the Philips Hue network(s)
    esp_zb_enable_joining_to_distributed(true);
    uint8_t secret_zll_trust_center_key[] = TRUST_CENTER_KEY;
    esp_zb_secur_TC_standard_distributed_key_set(secret_zll_trust_center_key);

    // Configure + start zigbee
    esp_zb_ep_list_t *light_ep = esp_zb_ep_list_create();
    esp_zb_cluster_list_t *cluster_list = light_config_clusters_create();
    esp_zb_endpoint_config_t endpoint_config = MY_EP_CONFIG();
    esp_zb_ep_list_add_ep(light_ep, cluster_list, endpoint_config);

    esp_zb_device_register(light_ep);
    esp_zb_core_action_handler_register(zb_action_handler);
    esp_zb_raw_command_handler_register(zb_raw_command_handler);
    esp_zb_set_primary_network_channel_set(ESP_ZB_TRANSCEIVER_ALL_CHANNELS_MASK);
    ESP_ERROR_CHECK(esp_zb_start(false));
    esp_zb_stack_main_loop();
}

void app_main(void)
{
    ESP_ERROR_CHECK(status_indicator_initialize());
    ESP_ERROR_CHECK(reset_button_initialize());

    esp_zb_platform_config_t config = {
        .radio_config = ESP_ZB_DEFAULT_RADIO_CONFIG(),
        .host_config = ESP_ZB_DEFAULT_HOST_CONFIG(),
    };

    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_zb_platform_config(&config));
    ESP_ERROR_CHECK(light_driver_initialize());
    ESP_ERROR_CHECK(light_config_initialize());

    xTaskCreate(esp_zb_task, "Zigbee_main", 4096, NULL, 5, NULL);
}
