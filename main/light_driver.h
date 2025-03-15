/*
 * ESP32 White Ambiance
 * Copyright © 2025 Michal Jirků (wejn)
 *
 * This code is licensed under GPL version 3.
 *
 * Purpose: Drives the PWM for the light, configured according to
 * global_config.
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "esp_err.h"

// Initialize light driver (keeps all channels off)
esp_err_t light_driver_initialize();

// Update channels based on light_config
esp_err_t light_driver_update();

#ifdef __cplusplus
} // extern "C"
#endif
