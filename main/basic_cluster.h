/*
 * ESP32 White Ambiance
 * Written in 2025 by Michal Jirků (wejn)
 *
 * This code is licensed under AGPL version 3.
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "esp_err.h"
#include "esp_check.h"
#include "esp_zigbee_core.h"

typedef struct basic_info_s {
    char *manufacturer_name; // up to 32 bytes
    char *model_identifier; // up to 32 bytes
	char *build_id; // up to 16 bytes, auto-filled in from app_desc->version
	char *date_code; // up to 16 bytes, optional
} basic_info_t;

esp_err_t populate_basic_cluster_info(esp_zb_ep_list_t *ep_list, uint8_t endpoint_id, basic_info_t *info);

#ifdef __cplusplus
} // extern "C"
#endif
