/*
 * ESP32 White Ambiance
 * Written in 2025 by Michal Jirků (wejn)
 *
 * This code is licensed under AGPL version 3.
 *
 * Purpose: State machine for the status indicator, based on connection status.
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Initialize status indicator (and dependencies)
esp_err_t status_indicator_initialize();

#ifdef __cplusplus
} // extern "C"
#endif
