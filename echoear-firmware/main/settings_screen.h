/*
 * Settings screen for the round panel: brightness arc (left), volume arc (right) and a
 * Voice (TTS) switch. It opens only by pulling down from the top edge past a threshold, so
 * stray swipes on the dashboard never land here; it closes on a swipe up, a tap on the hint,
 * or after a quiet spell.
 *
 * Pure LVGL (no BSP calls) so the host simulator renders the same code. Not thread-safe:
 * hold the display lock when calling these.
 */
#pragma once

#include <stdbool.h>
#include "lvgl.h"

/* A pull counts only if the finger first touches within this distance of the top edge. */
#define SETTINGS_PULL_EDGE_PX 56
/* Brightness never goes below this, so the screen cannot be turned dark from itself. */
#define SETTINGS_BRIGHTNESS_MIN 10

typedef struct {
    int brightness; /* SETTINGS_BRIGHTNESS_MIN..100 % */
    int volume;     /* 0..100 % */
    bool voice;     /* spoken announcements and chatter (chimes always play) */
} settings_values_t;

typedef enum {
    SETTINGS_FIELD_BRIGHTNESS,
    SETTINGS_FIELD_VOLUME,
    SETTINGS_FIELD_VOICE,
} settings_field_t;

/*
 * `field` changed. `final` is false while an arc is still being dragged and true once the
 * finger lifts or the switch flips: the moment to persist.
 */
typedef void (*settings_change_cb_t)(settings_field_t field, const settings_values_t *values, bool final);
typedef void (*settings_closed_cb_t)(void);

/* Builds the grabber pill, the pull indicator and the (hidden) screen on `parent`. */
void settings_screen_create(lv_obj_t *parent, const settings_values_t *initial, settings_change_cb_t on_change,
                            settings_closed_cb_t on_closed);

/* The finger that started at the top edge is now `distance` px lower: show the pull progress. */
void settings_screen_pull(int32_t distance);
/* The finger lifted: opens the screen past the threshold, otherwise springs back. */
void settings_screen_pull_release(int32_t distance);

bool settings_screen_is_open(void);
/* Slides the screen away (no-op when closed). */
void settings_screen_close(void);
