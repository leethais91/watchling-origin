/*
 * Host stand-in for the esp_vocat BSP so ui.c builds in the simulator. The simulator is
 * single-threaded, so the display lock is a no-op.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define BSP_LCD_H_RES 360
#define BSP_LCD_V_RES 360

static inline bool bsp_display_lock(uint32_t timeout_ms)
{
    (void)timeout_ms;
    return true;
}

static inline void bsp_display_unlock(void)
{
}
