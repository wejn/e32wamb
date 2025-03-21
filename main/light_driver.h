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

typedef enum ld_effect_type {
	LD_Effect_None, // none
	LD_Effect_Blink, // identify: flash once
	LD_Effect_Breathe, // identify: on/off over 1s, repeated 15x
	LD_Effect_Okay, // identify: flash twice
	LD_Effect_ChannelChange, // identify: max brightness 0.5s, then min brightness for 7.5s
	LD_Effect_Finish, // identify: finish current sequence
	LD_Effect_Stop, // identify: terminate asap
	LD_Effect_DelayedOff0, // fade to off in 0.8s
	LD_Effect_DelayedOff1, // no fade (??)
	LD_Effect_DelayedOff2, // off with effect: 50% dim down in 0.8s, then fade to off in 12s
	LD_Effect_DyingLight0, // off with effect: 20% dim up in 0.5s, then fade to off in 1s
} ld_effect_type;

// Initialize light driver (keeps all channels off)
esp_err_t light_driver_initialize();

// Trigger effect
esp_err_t light_driver_trigger_effect(const ld_effect_type effect);

// Update channels based on light_config
esp_err_t light_driver_update();

#ifdef __cplusplus
} // extern "C"
#endif
