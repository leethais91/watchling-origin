/*
 * Settings kept in NVS (namespace "settings") so they survive a restart.
 * Requires nvs_flash_init(); falls back to the defaults when NVS is unavailable.
 */
#pragma once

#include "settings_screen.h"

/* Fills `out` with the stored values, or the defaults for any that are missing. */
void settings_store_load(settings_values_t *out);
void settings_store_save(const settings_values_t *values);
