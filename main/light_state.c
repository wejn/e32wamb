/*
 * ESP32 White Ambiance
 * Written in 2025 by Michal Jirků (wejn)
 *
 * This code is licensed under AGPL version 3.
 */
#include "light_state.h"

// Current light state -- globals
volatile bool g_onoff;
volatile uint8_t g_level;
volatile uint16_t g_temperature;
