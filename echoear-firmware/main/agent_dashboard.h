/*
 * Idle dashboard for the round 360x360 panel: clock, link icons, Claude/Codex usage
 * arcs, animated robot head (robot_face.c), today's cost and a one-line status.
 *
 * Pure LVGL (no BSP calls) so the host simulator renders the same code. Like LVGL
 * itself, these functions are not thread-safe: hold the display lock when calling them.
 */
#pragma once

#include <stdbool.h>
#include "lvgl.h"

typedef enum {
    AGENT_DASHBOARD_TONE_NEUTRAL,
    AGENT_DASHBOARD_TONE_OK,
    AGENT_DASHBOARD_TONE_WARN,
    AGENT_DASHBOARD_TONE_ERROR,
} agent_dashboard_tone_t;

typedef struct {
    /* Percentages 0..100, or -1 when unknown. Weekly values only show when known. */
    int claude_usage_percent;
    int codex_usage_percent;
    int claude_weekly_percent;
    int codex_weekly_percent;

    float daily_cost; /* < 0 when unknown */

    /* Wi-Fi and BLE icons show only while connected (neither radio exists yet). */
    bool wifi_connected;
    bool ble_connected;
    bool host_linked;    /* USB bridge answering pings */
    int battery_percent; /* -1 hides the battery indicator */
    bool battery_charging;

    char time_text[8];  /* "10:24" */
    char date_text[20]; /* "Mon, Dec 9" */
    char status_text[64];
    agent_dashboard_tone_t status_tone;
} agent_dashboard_data_t;

/* Placeholder values shown until real providers exist. */
void agent_dashboard_mock_data(agent_dashboard_data_t *out);

/* Builds the widgets once on `parent` (normally the active screen) and returns the root. */
lv_obj_t *agent_dashboard_create(lv_obj_t *parent);

/* Applies `data`; only widgets whose value changed are invalidated. */
void agent_dashboard_update(const agent_dashboard_data_t *data);
