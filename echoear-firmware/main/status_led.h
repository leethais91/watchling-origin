/*
 * Green status LED on the mic board. It blinks while the robot needs the user, so a request
 * still shows when the screen is dozing or the robot lies on its face.
 */
#pragma once

#include <stdbool.h>
#include "esp_err.h"

esp_err_t status_led_init(void);

/* Blinking while true, off otherwise. Safe from any task; no-op if init failed. */
void status_led_set_attention(bool on);
