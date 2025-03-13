/*
 * ESP32 White Ambiance
 * Written in 2025 by Michal Jirků (wejn)
 *
 * This code is licensed under AGPL version 3.
 *
 * Purpose: Handles the factory reset button. Shorting it to GND for 5+ seconds
 * will trigger factory reset.
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "esp_err.h"

// Initialize reset button handler
esp_err_t reset_button_initialize();

#ifdef __cplusplus
} // extern "C"
#endif
