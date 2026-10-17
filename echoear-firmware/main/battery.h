/*
 * BQ27220 fuel gauge on the BSP I2C bus (the BSP does not drive it). Reads use the
 * gauge's standard commands, so no configuration or calibration is written.
 */
#pragma once

#include <stdbool.h>
#include "esp_err.h"

typedef struct {
    int percent;   /* state of charge, 0..100 */
    bool charging; /* current flowing into the battery */
} battery_state_t;

/* Probes the gauge; ESP_ERR_NOT_FOUND when it does not answer. Call after bsp_display_start(). */
esp_err_t battery_init(void);

esp_err_t battery_read(battery_state_t *out);
