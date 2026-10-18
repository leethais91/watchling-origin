/*
 * Animated robot head for the dashboard: a pre-rendered helmet with an empty visor and
 * LVGL-drawn eyes that blink, glance and change expression with the agents' state.
 *
 * Pure LVGL (no BSP calls) so the host simulator renders the same code. Not thread-safe:
 * hold the display lock when calling these.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "lvgl.h"

typedef enum {
    /* Base moods: what the robot does when nothing new happened. */
    ROBOT_MOOD_IDLE,  /* random blinks and the odd glance */
    ROBOT_MOOD_BUSY,  /* an agent is working: eyes sweep side to side */
    ROBOT_MOOD_SLEEP, /* no bridge or dozing: closed lids breathing, z's floating up */
    /* Reactions to agent events. */
    ROBOT_MOOD_HAPPY,     /* task finished */
    ROBOT_MOOD_ATTENTION, /* waiting for the user */
    ROBOT_MOOD_ERROR,
    ROBOT_MOOD_SURPRISED, /* poked: wide round eyes and a hop */
} robot_mood_t;

/* Where the robot looks while reacting: Claude's arc is on the left, Codex's on the right. */
typedef enum {
    ROBOT_LOOK_CENTER,
    ROBOT_LOOK_LEFT,
    ROBOT_LOOK_RIGHT,
} robot_look_t;

/* Builds the head on `parent`, centred at offset (x, y) from the parent's centre. Starts idle. */
lv_obj_t *robot_face_create(lv_obj_t *parent, int32_t x, int32_t y);

/* Sets the base mood. A change also ends any running reaction. */
void robot_face_set_base(robot_mood_t mood);

/* Shows a reaction for `hold_ms` (0 = until the next reaction or base change), then the base. */
void robot_face_react(robot_mood_t mood, robot_look_t look, uint32_t hold_ms);

/*
 * Fullscreen: drops the helmet and draws the eyes 3x larger, centred on the parent, so the
 * round glass itself becomes the visor. Off puts the head back where it was created.
 */
void robot_face_set_fullscreen(bool on);
