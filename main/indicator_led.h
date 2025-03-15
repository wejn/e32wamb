/*
 * ESP32 White Ambiance
 * Copyright © 2025 Michal Jirků (wejn)
 *
 * This code is licensed under GPL version 3.
 *
 * Purpose: Drives the RGB indicator led.
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "esp_err.h"

typedef enum indicator_state {
	IS_initial,
	IS_commissioning,
	IS_connected_no_coord,
	IS_connected,
	IS_reset_pending,
} indicator_state;

// Initialize indicator LED
esp_err_t indicator_led_initialize();

// Update indicator to given state
esp_err_t indicator_led_switch(indicator_state state);

// Lock indicator to given state
esp_err_t indicator_led_lock(indicator_state state);

// Unlock indicator to previous state
esp_err_t indicator_led_unlock();

#ifdef __cplusplus
} // extern "C"
#endif
