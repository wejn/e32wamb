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

typedef enum delayed_save_type {
	DS_onoff,
	DS_level,
	DS_temperature,
} delayed_save_type;

void trigger_delayed_save(delayed_save_type type, uint32_t value);

void create_delayed_save_task();

#ifdef __cplusplus
} // extern "C"
#endif
