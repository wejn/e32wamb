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

// Light config for state tracking & zibgbee cluster creation
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
    X(startup_temperature)

#define LCFV_AS_ENUM(NAME) LCFV_##NAME,
typedef enum lc_flash_var_s {
    _LCFV_ITER(LCFV_AS_ENUM)
} lc_flash_var_t;
#undef LCFV_AS_ENUM

// Save num variables to nvs at the same time
//
// Normally taken care of by light_config_update()
esp_err_t light_config_persist_vars(lc_flash_var_t *vars, size_t num);

// Save given variable (key) to nvs
//
// Normally taken care of by light_config_update()
esp_err_t light_config_persist_var(lc_flash_var_t key);

// Erase all keys from nvs
esp_err_t light_config_erase_flash();

// Create zigbee light clusters based on the config
esp_zb_cluster_list_t *light_config_clusters_create();

// Initialize light config and dependent subsystems
//
// This should be called *after* NVS is initialized, but before anyone
// touches light_config.
esp_err_t light_config_initialize();

// Update given writeable variable
esp_err_t light_config_update(lc_flash_var_t key, uint32_t val);

#ifdef __cplusplus
} // extern "C"
#endif
