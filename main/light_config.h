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

// Light config for cluster creation
typedef struct light_config_s {
    // Basic cluster
    char *manufacturer_name; // [R] up to 32 bytes
    char *model_identifier; // [R] up to 32 bytes
    char *build_id; // [R] up to 16 bytes, auto-filled in from app_desc->version if NULL
    char *date_code; // [R] up to 16 bytes, optional
    uint8_t power_source; // [R] 0x01 = mains, 0x03 = battery

    // OnOff cluster
    bool onoff; // [RPS] OnOff, on or off
    uint8_t startup_onoff; // [RW] 0 = off, 1 = on, 2 = toggle, 0x3..0xfe = preserved (no action), 0xff = previous value

    // Level cluster
    uint8_t level_options; // [RW] (bitfield) 1 = ExecuteIfOff, 2 = Couple changes to level with Color Temperature
    uint8_t level; // [RPS] CurrentLevel: 1-254, 255 = uknown, 0 = do not use(!)
    uint8_t startup_level; // [RW] StartUpCurrentLevel: 0 = minimum, 0xff = previous, rest = this value

    // Color cluster
    uint8_t color_options; // [RW] 1 = ExecuteIfOff
    uint16_t temperature; // [RPS] ColorTemperatureMireds: in mireds
    uint16_t startup_temperature; // [RW] StartUpColorTemperatureMireds: 0xffff = previous, 0x0000 - 0xffef = this value
    uint16_t min_temperature; // [R] ColorTempPhysicalMinMireds
    uint16_t max_temperature; // [R] ColorTempPhysicalMaxMireds
    uint16_t couple_min_temperature; // [R] CoupleColorTempToLevelMinMireds: temperature for level 0xfe
} light_config_t;

// Global accessor of the light config
extern const light_config_t * const light_config;
// FIXME: make this happen; I'm thinking:
// - Init by passing in a pointer (with sane defaults), memcopy in place; careful with the pointers to strings
// - Spinlock for changes
// - Gating function to write some of the variables
//   (which also forces NVS saves, delayed saves, etc)
// - Question is whether the gating function also invokes PWM update, or whether it provides some post-update callback

// All the flash variables we'll be storing (used for enum and to_string),
// all of them will get stored in uint32_t. All generated with LCFV_ prefix.
#define _LCFV_ITER(X) \
    X(onoff) \
    X(startup_onoff) \
    X(level_options) \
    X(level) \
    X(startup_level) \
    X(color_options) \
    X(temperature) \
    X(startup_temp)

#define LCFV_AS_ENUM(NAME) LCFV_##NAME,
typedef enum lc_flash_var_s {
    _LCFV_ITER(LCFV_AS_ENUM)
} lc_flash_var_t;
#undef LCFV_AS_ENUM

typedef struct lc_flash_vars_s {
    lc_flash_var_t key;
    uint32_t value;
} lc_flash_vars_t;

// Save num variables to nvs at the same time
esp_err_t lc_persist_vars(lc_flash_vars_t *vars, size_t num);

// Save given variable (key) to nvs
esp_err_t lc_persist_var(lc_flash_var_t key, uint32_t val);

// Erase all keys from nvs
esp_err_t lc_erase_flash();

// Restore read-write variables of light_config_t from nvs
esp_err_t lc_restore_cfg_from_flash(light_config_t *light_cfg);

// Create light clusters based on the config
esp_zb_cluster_list_t *lc_clusters_create(light_config_t *light_cfg);

#ifdef __cplusplus
} // extern "C"
#endif
