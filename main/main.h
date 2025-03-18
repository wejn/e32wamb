/*
 * ESP32 White Ambiance
 * Copyright © 2025 Michal Jirků (wejn)
 *
 * This code is licensed under GPL version 3.
 *
 * Purpose: Main app code (and zigbee handler).
 */

#pragma once

#include "global_config.h"

// When was the last time our light endpoint was last queried.
// Records timestamp from esp_timer_get_time(), initially zero.
extern uint64_t light_endpoint_last_queried_time;
