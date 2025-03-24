/*
 * ESP32 White Ambiance
 * Copyright © 2025 Michal Jirků (wejn)
 *
 * This code is licensed under GPL version 3.
 *
 * Purpose: State machine for the status indicator, based on connection status.
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "esp_err.h"

// Initialize status indicator (and dependencies)
esp_err_t status_indicator_initialize();

#ifdef __cplusplus
} // extern "C"
#endif
