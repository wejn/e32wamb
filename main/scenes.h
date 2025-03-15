/*
 * ESP32 White Ambiance
 * Copyright © 2025 Michal Jirků (wejn)
 *
 * This code is licensed under GPL version 3.
 *
 * Purpose: Scene handler callbacks for the zigbee handler.
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "esp_err.h"
#include "esp_check.h"
#include "esp_zigbee_core.h"

// Store scene callback
esp_err_t store_scene(esp_zb_zcl_store_scene_message_t *msg);

// Recall scene callback
esp_err_t recall_scene(esp_zb_zcl_recall_scene_message_t *msg);

#ifdef __cplusplus
} // extern "C"
#endif
