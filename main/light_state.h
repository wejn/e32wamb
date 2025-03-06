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

// Current light state
extern volatile bool g_onoff;
extern volatile uint8_t g_level;
extern volatile uint16_t g_temperature;

#ifdef __cplusplus
} // extern "C"
#endif
