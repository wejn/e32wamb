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

// Delayed save types (variables) that are suported.
typedef enum delayed_save_type {
	DS_onoff,
	DS_level,
	DS_temperature,
} delayed_save_type;

// Trigger delayed save for a given variable type with given value.
void trigger_delayed_save(delayed_save_type type, uint32_t value);

// Create delayed save task that gets triggered by trigger_delayed_save().
// Must be created before trigger_delayed_save() is called.
void create_delayed_save_task();

#ifdef __cplusplus
} // extern "C"
#endif
