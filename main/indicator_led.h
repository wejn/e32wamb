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
	IS_initial, // Initial state
	IS_commissioning, // Commissioning state (not connected yet)
	IS_connected_no_coord, // Connected to network, but nobody is querying us & no coordinator
	IS_connected, // Fully connected
	IS_reset_pending, // Reset button held (so we'll be resetting soon)
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
