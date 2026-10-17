#include "ui.h"

#include <stdio.h>
#include <string.h>
#include <time.h>
#include "bsp/esp-bsp.h"
#include "lvgl.h"

#include "agent_dashboard.h"
#include "assets/dashboard_assets.h"
#include "robot_face.h"

/*
 * The idle dashboard lives in agent_dashboard.c; this file owns the confirm modal and
 * feeds the dashboard from the bridge. s_dash is only touched under the display lock.
 */

#define COLOR_BG          0x0B0F19
#define COLOR_MUTED       0x9CA3AF
#define COLOR_OK          0x22C55E
#define COLOR_WARN        0xF59E0B
#define COLOR_DANGER      0xEF4444

#define MODAL_RING_PX     8
/*
 * The decision buttons run past the round edge on purpose: the glass slices off their
 * outer sides. Labels shift inward so the text stays centred in the visible part.
 */
#define BUTTON_W          162
#define BUTTON_H          60
#define BUTTON_RADIUS     (BUTTON_H * 30 / 100)
#define BUTTON_GAP        10
#define BUTTON_OFFSET_X   ((BUTTON_W + BUTTON_GAP) / 2)
#define BUTTON_OFFSET_Y   92
#define BUTTON_LABEL_INSET 10
#define SUMMARY_W         290
#define SUMMARY_H         144
#define SUMMARY_Y         92
#define RING_PULSE_MS     500
#define STATUS_POLL_MS    1000
/* Claude Code fires no hook when a turn is interrupted, so a busy flag also runs out. */
#define BUSY_TIMEOUT_MS   (15 * 60 * 1000)
#define HAPPY_MS          4000
#define ERROR_MS          6000
#define ATTENTION_MS      (2 * 60 * 1000)

enum { AGENT_CLAUDE, AGENT_CODEX, AGENT_COUNT };

static ui_decision_cb_t s_on_decision;
static ui_link_probe_t s_link_probe;

static agent_dashboard_data_t s_dash;
/* Set once the bridge has synced the clock; until then the dashboard shows "--:--". */
static bool s_clock_synced;
static int s_utc_offset_min;
/* lv_tick of the latest "busy" per agent; 0 = idle. */
static uint32_t s_busy_since[AGENT_COUNT];

static lv_obj_t *s_modal;
static lv_obj_t *s_modal_title;
static lv_obj_t *s_modal_tool;
static lv_obj_t *s_modal_summary;
static uint32_t s_modal_id;
static bool s_modal_visible;

/* ---------- confirm modal ---------- */

static void modal_hide(void)
{
    s_modal_visible = false;
    lv_anim_delete(s_modal, NULL);
    lv_obj_set_hidden(s_modal, true);
}

static void ring_opa_anim_cb(void *obj, int32_t value)
{
    lv_obj_set_style_border_opa(obj, (lv_opa_t)value, 0);
}

static void decision_button_cb(lv_event_t *e)
{
    if (!s_modal_visible) {
        return;
    }
    bool approve = (bool)(intptr_t)lv_event_get_user_data(e);
    uint32_t id = s_modal_id;
    modal_hide();
    s_on_decision(id, approve);
}

static lv_obj_t *decision_button_create(lv_obj_t *parent, const char *text, uint32_t color,
                                        int32_t x, bool approve)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_size(btn, BUTTON_W, BUTTON_H);
    lv_obj_set_style_radius(btn, BUTTON_RADIUS, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(color), 0);
    lv_obj_align(btn, LV_ALIGN_CENTER, x, BUTTON_OFFSET_Y);
    lv_obj_add_event_cb(btn, decision_button_cb, LV_EVENT_CLICKED, (void *)(intptr_t)approve);

    lv_obj_t *label = lv_label_create(btn);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_20, 0);
    lv_label_set_text(label, text);
    lv_obj_align(label, LV_ALIGN_CENTER, x < 0 ? BUTTON_LABEL_INSET : -BUTTON_LABEL_INSET, 0);
    return btn;
}

static void modal_create(void)
{
    /* Top layer so it covers the dashboard and swallows touches meant for it. */
    s_modal = lv_obj_create(lv_layer_top());
    lv_obj_set_size(s_modal, BSP_LCD_H_RES, BSP_LCD_V_RES);
    lv_obj_center(s_modal);
    lv_obj_set_style_radius(s_modal, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_modal, lv_color_hex(COLOR_BG), 0);
    lv_obj_set_style_bg_opa(s_modal, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_modal, MODAL_RING_PX, 0);
    lv_obj_set_style_pad_all(s_modal, 0, 0);
    lv_obj_set_scrollable(s_modal, false);

    s_modal_title = lv_label_create(s_modal);
    lv_obj_set_style_text_color(s_modal_title, lv_color_hex(COLOR_MUTED), 0);
    lv_obj_align(s_modal_title, LV_ALIGN_TOP_MID, 0, 40);

    s_modal_tool = lv_label_create(s_modal);
    lv_obj_set_style_text_font(s_modal_tool, &lv_font_montserrat_20, 0);
    lv_obj_align(s_modal_tool, LV_ALIGN_TOP_MID, 0, 62);

    /* Long commands scroll by dragging inside this box. */
    lv_obj_t *box = lv_obj_create(s_modal);
    /* Fills the band between the tool name and the buttons. */
    lv_obj_set_size(box, SUMMARY_W, SUMMARY_H);
    lv_obj_align(box, LV_ALIGN_TOP_MID, 0, SUMMARY_Y);
    lv_obj_set_style_bg_opa(box, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(box, 0, 0);
    lv_obj_set_style_pad_all(box, 4, 0);
    lv_obj_set_scrollbar_mode(box, LV_SCROLLBAR_MODE_AUTO);

    s_modal_summary = lv_label_create(box);
    lv_obj_set_width(s_modal_summary, lv_pct(100));
    lv_label_set_long_mode(s_modal_summary, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_text_font(s_modal_summary, &font_mono_15, 0);
    lv_obj_set_style_text_color(s_modal_summary, lv_color_white(), 0); /* the theme darkens text in the box */
    lv_obj_set_style_text_align(s_modal_summary, LV_TEXT_ALIGN_CENTER, 0);

    decision_button_create(s_modal, "DENY", COLOR_DANGER, -BUTTON_OFFSET_X, false);
    decision_button_create(s_modal, "APPROVE", COLOR_OK, BUTTON_OFFSET_X, true);

    lv_obj_set_hidden(s_modal, true);
}

static void modal_show(uint32_t id, const char *agent_name, const char *tool, const char *summary, ui_risk_t risk)
{
    uint32_t ring = risk == UI_RISK_HIGH ? COLOR_DANGER : COLOR_WARN;
    s_modal_id = id;
    lv_obj_set_style_border_color(s_modal, lv_color_hex(ring), 0);
    lv_obj_set_style_text_color(s_modal_tool, lv_color_hex(ring), 0);
    lv_label_set_text(s_modal_title, agent_name);
    lv_label_set_text(s_modal_tool, tool);
    lv_label_set_text(s_modal_summary, summary);
    lv_obj_scroll_to_y(lv_obj_get_parent(s_modal_summary), 0, LV_ANIM_OFF);

    /* Pulse the edge ring until the request is answered. */
    lv_anim_delete(s_modal, NULL);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_modal);
    lv_anim_set_exec_cb(&a, ring_opa_anim_cb);
    lv_anim_set_values(&a, LV_OPA_30, LV_OPA_COVER);
    lv_anim_set_duration(&a, RING_PULSE_MS);
    lv_anim_set_reverse_duration(&a, RING_PULSE_MS);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&a);

    lv_obj_set_hidden(s_modal, false);
    s_modal_visible = true;
}

static void clock_format(void)
{
    time_t local = time(NULL) + (time_t)s_utc_offset_min * 60;
    struct tm tm;
    gmtime_r(&local, &tm);
    strftime(s_dash.time_text, sizeof(s_dash.time_text), "%H:%M", &tm);
    char day_month[12];
    strftime(day_month, sizeof(day_month), "%a, %b", &tm);
    snprintf(s_dash.date_text, sizeof(s_dash.date_text), "%s %d", day_month, tm.tm_mday);
}

/* snprintf cuts by bytes; drop a multi-byte UTF-8 character it left incomplete at the end. */
static void utf8_trim_partial(char *text)
{
    size_t len = strlen(text);
    size_t start = len;
    while (start > 0 && ((unsigned char)text[start - 1] & 0xC0) == 0x80) {
        start--; /* skip continuation bytes back to the lead byte */
    }
    if (start == 0) {
        return;
    }
    unsigned char lead = (unsigned char)text[start - 1];
    size_t need = lead >= 0xF0 ? 4 : lead >= 0xE0 ? 3 : lead >= 0xC0 ? 2 : 1;
    if (len - (start - 1) < need) {
        text[start - 1] = '\0';
    }
}

static int agent_index(const char *source)
{
    return strcmp(source, "codex") == 0 ? AGENT_CODEX : AGENT_CLAUDE;
}

/* Base mood: asleep without the bridge, scanning while any agent works, idle otherwise. */
static void robot_base_update(void)
{
    bool busy = false;
    for (int i = 0; i < AGENT_COUNT; i++) {
        if (s_busy_since[i] != 0 && lv_tick_elaps(s_busy_since[i]) > BUSY_TIMEOUT_MS) {
            s_busy_since[i] = 0;
        }
        busy |= s_busy_since[i] != 0;
    }
    robot_face_set_base(!s_dash.host_linked ? ROBOT_MOOD_SLEEP : busy ? ROBOT_MOOD_BUSY : ROBOT_MOOD_IDLE);
}

/* Runs every second in the LVGL task; the dashboard only redraws what changed. */
static void status_timer_cb(lv_timer_t *timer)
{
    bool linked = s_link_probe();
    s_dash.host_linked = linked;
    if (s_clock_synced) {
        clock_format();
    }
    agent_dashboard_update(&s_dash);
    robot_base_update();
    if (!linked && s_modal_visible) {
        modal_hide(); /* the bridge releases the request to the terminal on its side */
    }
}

/* ---------- public API ---------- */

void ui_init(ui_decision_cb_t on_decision, ui_link_probe_t link_probe)
{
    s_on_decision = on_decision;
    s_link_probe = link_probe;

    bsp_display_lock(0);
    lv_obj_set_style_bg_color(lv_screen_active(), lv_color_black(), 0);
    agent_dashboard_create(lv_screen_active());
    /* Start from the mock values for usage/cost/status, but show nothing the device cannot know. */
    agent_dashboard_mock_data(&s_dash);
    s_dash.time_text[0] = '\0';
    s_dash.date_text[0] = '\0';
    s_dash.wifi_connected = false;
    s_dash.ble_connected = false;
    s_dash.battery_percent = -1;
    agent_dashboard_update(&s_dash);
    robot_base_update();
    modal_create();
    lv_timer_create(status_timer_cb, STATUS_POLL_MS, NULL);
    bsp_display_unlock();
}

void ui_set_usage(const ui_usage_t *usage)
{
    bsp_display_lock(0);
    s_dash.claude_usage_percent = usage->claude_h5;
    s_dash.claude_weekly_percent = usage->claude_d7;
    s_dash.codex_usage_percent = usage->codex_h5;
    s_dash.codex_weekly_percent = usage->codex_d7;
    s_dash.daily_cost = usage->cost_today;
    agent_dashboard_update(&s_dash);
    bsp_display_unlock();
}

void ui_set_clock_offset(int utc_offset_min)
{
    bsp_display_lock(0);
    s_utc_offset_min = utc_offset_min;
    s_clock_synced = true;
    clock_format();
    agent_dashboard_update(&s_dash);
    bsp_display_unlock();
}

void ui_set_battery(int percent, bool charging)
{
    bsp_display_lock(0);
    s_dash.battery_percent = percent;
    s_dash.battery_charging = charging;
    agent_dashboard_update(&s_dash);
    bsp_display_unlock();
}

void ui_show_event(const char *source, ui_event_kind_t kind, const char *text)
{
    static const agent_dashboard_tone_t tones[] = {
        [UI_EVENT_DONE] = AGENT_DASHBOARD_TONE_NEUTRAL, /* replaced by the agent's colour below */
        [UI_EVENT_ATTENTION] = AGENT_DASHBOARD_TONE_WARN,
        [UI_EVENT_ERROR] = AGENT_DASHBOARD_TONE_ERROR,
    };
    static const struct {
        robot_mood_t mood;
        uint32_t hold_ms;
    } reactions[] = {
        [UI_EVENT_DONE] = {ROBOT_MOOD_HAPPY, HAPPY_MS},
        [UI_EVENT_ATTENTION] = {ROBOT_MOOD_ATTENTION, ATTENTION_MS},
        [UI_EVENT_ERROR] = {ROBOT_MOOD_ERROR, ERROR_MS},
    };
    int agent = agent_index(source);

    bsp_display_lock(0);
    /* No "Claude:" prefix: the colour (done) and the robot's gaze tell the agents apart. */
    snprintf(s_dash.status_text, sizeof(s_dash.status_text), "%s", text);
    utf8_trim_partial(s_dash.status_text);
    s_dash.status_tone = kind != UI_EVENT_DONE ? tones[kind]
                         : agent == AGENT_CODEX ? AGENT_DASHBOARD_TONE_CODEX
                                                : AGENT_DASHBOARD_TONE_CLAUDE;
    agent_dashboard_update(&s_dash);
    /* Every event ends that agent's turn. Claude's arc is on the left, Codex's on the right. */
    s_busy_since[agent] = 0;
    robot_base_update();
    robot_face_react(reactions[kind].mood, agent == AGENT_CODEX ? ROBOT_LOOK_RIGHT : ROBOT_LOOK_LEFT,
                     reactions[kind].hold_ms);
    bsp_display_unlock();
}

void ui_set_activity(const char *source, bool busy)
{
    bsp_display_lock(0);
    /* 0 means idle, so a busy flag set at tick 0 is stored as 1. */
    s_busy_since[agent_index(source)] = busy ? LV_MAX(lv_tick_get(), 1) : 0;
    robot_base_update();
    if (busy) {
        /* A new turn replaces any reaction still showing (the base may already be busy). */
        robot_face_react(ROBOT_MOOD_BUSY, ROBOT_LOOK_CENTER, 0);
    }
    bsp_display_unlock();
}

void ui_show_confirm(uint32_t id, const char *source, const char *tool, const char *summary, ui_risk_t risk)
{
    bsp_display_lock(0);
    modal_show(id, agent_index(source) == AGENT_CODEX ? "Codex" : "Claude", tool, summary, risk);
    bsp_display_unlock();
}

void ui_cancel_confirm(uint32_t id)
{
    bsp_display_lock(0);
    if (s_modal_visible && s_modal_id == id) {
        modal_hide();
    }
    bsp_display_unlock();
}
