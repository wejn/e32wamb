/*
 * ESP32 White Ambiance
 * Copyright © 2025 Michal Jirků (wejn)
 *
 * This code is licensed under GPL version 3.
 *
 * Purpose: Configures the XIAO ESP32-C6 RF switch.
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "esp_err.h"

// Initialize the rf switch
esp_err_t rf_switch_initialize();

#ifdef __cplusplus
} // extern "C"
#endif
