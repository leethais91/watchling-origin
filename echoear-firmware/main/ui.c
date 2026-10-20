#include "ui.h"

#include <stdio.h>
#include <string.h>
#include <time.h>
#include "bsp/esp-bsp.h"
#include "lvgl.h"

#include "agent_dashboard.h"
#include "assets/dashboard_assets.h"
#include "robot_face.h"
#include "settings_screen.h"

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
#define MODAL_LOGO_Y      34
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
#define COLOR_PAIRING     0x167DFF
#define PAIRING_TITLE_Y   -52
#define PAIRING_HINT_Y    46
/* Claude Code fires no hook when a turn is interrupted, so a busy flag also runs out. */
#define BUSY_TIMEOUT_MS   (15 * 60 * 1000)
#define HAPPY_MS          4000
#define ERROR_MS          6000
#define ATTENTION_MS      (2 * 60 * 1000)
/*
 * Quiet time before the dashboard gives way to the fullscreen face, and before the robot
 * dozes off. Only agent events, requests and touches count: usage and clock frames
 * arrive on their own all the time.
 */
#define FACE_IDLE_MS      (30 * 1000)
#define DOZE_IDLE_MS      (5 * 60 * 1000)
/* On battery with no USB power and no bridge, the backlight goes off after this long. */
#ifndef SCREEN_OFF_IDLE_MS
#define SCREEN_OFF_IDLE_MS (10 * 60 * 1000)
#endif
#define SURPRISED_MS      1200
/* A lean lasts until the body is level again; this only covers a missed LEVEL event. */
#define TILT_LEAN_MAX_MS  (20 * 1000)
#define DIZZY_MS          2800
#define GRAVITY_POLL_MS   100 /* the accelerometer is polled at 10 Hz */
/* Rocking: this many tilts, each the other way and within ROCK_GAP_MS of the last, cheers it up. */
#define ROCK_SWINGS       3
#define ROCK_GAP_MS       2500
#define ROCK_HAPPY_MS     2500
#define QUIET_REACT_MS    1500

enum { AGENT_CLAUDE, AGENT_CODEX, AGENT_COUNT };

static ui_decision_cb_t s_on_decision;
static ui_link_probe_t s_link_probe;

static agent_dashboard_data_t s_dash;
/* Set once the bridge has synced the clock; until then the dashboard shows "--:--". */
static bool s_clock_synced;
static int s_utc_offset_min;
/* lv_tick of the latest "busy" per agent; 0 = idle. */
static uint32_t s_busy_since[AGENT_COUNT];

/* Bridge answering on USB or BLE; asleep otherwise. */
static bool s_bridge_linked;

/* lv_tick of the latest agent event, request or touch. */
static uint32_t s_last_activity;
/* lv_tick of the latest status poll that saw a link or USB power. */
static uint32_t s_last_powered;
static ui_screen_off_cb_t s_on_screen_off;
static bool s_screen_off;
/* From the fuel gauge: current flows out of the battery (false when unknown). */
static bool s_battery_discharging;
/* Covers everything while the screen is dark, so the waking touch presses nothing. */
static lv_obj_t *s_wake_layer;
static bool s_face_mode;

/* Rocking detection: direction (-1 left, 1 right) and lv_tick of the latest tilt. */
static int s_rock_dir;
static uint32_t s_rock_at;
static int s_rock_swings;

/* lv_tick of an attention event nobody has answered by touch yet; 0 = none. */
static uint32_t s_attention_since;
static bool s_needs_user;
static ui_needs_user_cb_t s_on_needs_user;
static ui_gravity_probe_t s_gravity_probe;

/* Touch-down y of a pull that started at the top edge; -1 while the touch is a plain poke. */
static int32_t s_pull_start_y = -1;

static lv_obj_t *s_pairing;
static lv_obj_t *s_pairing_code;

static lv_obj_t *s_modal;
static lv_obj_t *s_modal_logo;
static lv_obj_t *s_modal_tool;
static lv_obj_t *s_modal_summary;
static uint32_t s_modal_id;
static bool s_modal_visible;

static void robot_base_update(void);

/* ---------- needs-user signal ---------- */

static void needs_user_update(void)
{
    if (s_attention_since != 0 && lv_tick_elaps(s_attention_since) >= ATTENTION_MS) {
        s_attention_since = 0;
    }
    bool needs = s_modal_visible || s_attention_since != 0;
    if (needs != s_needs_user) {
        s_needs_user = needs;
        if (s_on_needs_user != NULL) {
            s_on_needs_user(needs);
        }
    }
}

/* The user touched the robot, so a pending "waiting for input" has been seen. */
static void attention_seen(void)
{
    s_attention_since = 0;
    needs_user_update();
}

/* ---------- face mode ---------- */

static void face_mode_set(bool on)
{
    if (on != s_face_mode) {
        s_face_mode = on;
        agent_dashboard_set_face_mode(on);
    }
}

/* Something happened: back to the dashboard, and the idle clock starts over. */
static void activity_note(void)
{
    s_last_activity = lv_tick_get();
    face_mode_set(false);
}

static int32_t touch_y(void)
{
    lv_point_t point;
    lv_indev_get_point(lv_indev_active(), &point);
    return point.y;
}

/*
 * A touch that starts at the top edge is a pull towards settings; any other touch pokes the
 * robot. Settings open only if nothing else (modal, pairing) took the screen meanwhile.
 */
static void touch_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_PRESSED) {
        activity_note();
        attention_seen();
        int32_t y = touch_y();
        if (y < SETTINGS_PULL_EDGE_PX) {
            s_pull_start_y = y;
            return;
        }
        robot_base_update(); /* wakes a dozing robot first; a base change ends any reaction */
        robot_face_react(ROBOT_MOOD_SURPRISED, ROBOT_LOOK_CENTER, SURPRISED_MS);
        return;
    }
    if (s_pull_start_y < 0) {
        return;
    }
    int32_t distance = touch_y() - s_pull_start_y;
    if (code == LV_EVENT_PRESSING) {
        settings_screen_pull(distance);
        return;
    }
    bool screen_free = code == LV_EVENT_RELEASED && !s_modal_visible && lv_obj_is_hidden(s_pairing);
    settings_screen_pull_release(screen_free ? distance : 0);
    s_pull_start_y = -1;
}

/*
 * The dashboard has nothing to tap, so one invisible layer takes every touch that the
 * settings, modal and pairing screens (created later, on top of it) do not.
 */
static void touch_catcher_create(void)
{
    lv_obj_t *catcher = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(catcher);
    lv_obj_set_size(catcher, BSP_LCD_H_RES, BSP_LCD_V_RES);
    lv_obj_center(catcher);
    static const lv_event_code_t codes[] = {LV_EVENT_PRESSED, LV_EVENT_PRESSING, LV_EVENT_RELEASED,
                                            LV_EVENT_PRESS_LOST};
    for (size_t i = 0; i < sizeof(codes) / sizeof(codes[0]); i++) {
        lv_obj_add_event_cb(catcher, touch_cb, codes[i], NULL);
    }
}

/* ---------- screen off on battery ---------- */

static void screen_off_set(bool off)
{
    if (off == s_screen_off || s_on_screen_off == NULL) {
        return;
    }
    s_screen_off = off;
    lv_obj_set_hidden(s_wake_layer, !off);
    s_on_screen_off(off);
    /* Nobody sees the face animate in the dark: stop redrawing and flushing it over SPI. */
    lv_display_t *display = lv_display_get_default();
    lv_display_enable_invalidation(display, !off);
    if (!off) {
        lv_obj_invalidate(lv_screen_active());
        lv_obj_invalidate(lv_layer_top());
    }
}

static void wake_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    activity_note();
    screen_off_set(false);
}

static void wake_layer_create(void)
{
    s_wake_layer = lv_obj_create(lv_layer_sys());
    lv_obj_remove_style_all(s_wake_layer);
    lv_obj_set_size(s_wake_layer, BSP_LCD_H_RES, BSP_LCD_V_RES);
    lv_obj_center(s_wake_layer);
    lv_obj_add_event_cb(s_wake_layer, wake_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_set_hidden(s_wake_layer, true);
}

/*
 * Dark only while the board runs on its battery (a charger or power bank without a USB host
 * reads as not discharging once full), with no USB host, no link, nothing on screen that
 * waits for the user, and no touch, motion or link for SCREEN_OFF_IDLE_MS.
 */
static void screen_power_update(ui_link_state_t link)
{
    bool linked = link.usb || link.ble;
    if (linked || link.usb_power) {
        s_last_powered = lv_tick_get();
    }
    bool on_battery = s_battery_discharging && !link.usb_power;
    bool idle = lv_tick_elaps(s_last_activity) >= SCREEN_OFF_IDLE_MS &&
                lv_tick_elaps(s_last_powered) >= SCREEN_OFF_IDLE_MS;
    screen_off_set(on_battery && !linked && idle && !s_modal_visible && lv_obj_is_hidden(s_pairing));
}

/* ---------- confirm modal ---------- */

static void modal_hide(void)
{
    s_modal_visible = false;
    lv_anim_delete(s_modal, NULL);
    lv_obj_set_hidden(s_modal, true);
    needs_user_update();
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
    activity_note();
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

    /* Which agent asks: its logo, above the tool name. */
    s_modal_logo = lv_image_create(s_modal);
    lv_obj_align(s_modal_logo, LV_ALIGN_TOP_MID, 0, MODAL_LOGO_Y);

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

static void modal_show(uint32_t id, const lv_image_dsc_t *agent_logo, const char *tool, const char *summary,
                       ui_risk_t risk)
{
    uint32_t ring = risk == UI_RISK_HIGH ? COLOR_DANGER : COLOR_WARN;
    settings_screen_close(); /* the request needs an answer; settings would wait behind it */
    s_modal_id = id;
    lv_obj_set_style_border_color(s_modal, lv_color_hex(ring), 0);
    lv_obj_set_style_text_color(s_modal_tool, lv_color_hex(ring), 0);
    lv_image_set_src(s_modal_logo, agent_logo);
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
    needs_user_update();
}

/* ---------- Bluetooth pairing ---------- */

/* Covers everything, the confirm modal included: pairing needs the user's full attention. */
static void pairing_create(void)
{
    s_pairing = lv_obj_create(lv_layer_top());
    lv_obj_set_size(s_pairing, BSP_LCD_H_RES, BSP_LCD_V_RES);
    lv_obj_center(s_pairing);
    lv_obj_set_style_radius(s_pairing, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_pairing, lv_color_hex(COLOR_BG), 0);
    lv_obj_set_style_bg_opa(s_pairing, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_pairing, MODAL_RING_PX, 0);
    lv_obj_set_style_border_color(s_pairing, lv_color_hex(COLOR_PAIRING), 0);
    lv_obj_set_style_pad_all(s_pairing, 0, 0);
    lv_obj_set_scrollable(s_pairing, false);

    lv_obj_t *title = lv_label_create(s_pairing);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(COLOR_PAIRING), 0);
    lv_label_set_text(title, LV_SYMBOL_BLUETOOTH " Pairing");
    lv_obj_align(title, LV_ALIGN_CENTER, 0, PAIRING_TITLE_Y);

    s_pairing_code = lv_label_create(s_pairing);
    lv_obj_set_style_text_font(s_pairing_code, &font_dash_bold_30, 0);
    lv_obj_set_style_text_color(s_pairing_code, lv_color_white(), 0);
    lv_obj_set_style_text_letter_space(s_pairing_code, 4, 0);
    lv_obj_align(s_pairing_code, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t *hint = lv_label_create(s_pairing);
    lv_obj_set_style_text_color(hint, lv_color_hex(COLOR_MUTED), 0);
    lv_label_set_text(hint, "Type this code on your Mac");
    lv_obj_align(hint, LV_ALIGN_CENTER, 0, PAIRING_HINT_Y);

    lv_obj_set_hidden(s_pairing, true);
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

/*
 * Base mood: asleep without the bridge, scanning while any agent works, dozing after a
 * long quiet spell, idle otherwise.
 */
static robot_mood_t robot_base_mood(void)
{
    bool busy = false;
    for (int i = 0; i < AGENT_COUNT; i++) {
        if (s_busy_since[i] != 0 && lv_tick_elaps(s_busy_since[i]) > BUSY_TIMEOUT_MS) {
            s_busy_since[i] = 0;
        }
        busy |= s_busy_since[i] != 0;
    }
    bool dozing = lv_tick_elaps(s_last_activity) >= DOZE_IDLE_MS;
    return !s_bridge_linked ? ROBOT_MOOD_SLEEP
           : busy           ? ROBOT_MOOD_BUSY
           : dozing         ? ROBOT_MOOD_SLEEP
                            : ROBOT_MOOD_IDLE;
}

static void robot_base_update(void)
{
    robot_face_set_base(robot_base_mood());
}

/* Runs every second in the LVGL task; the dashboard only redraws what changed. */
static void status_timer_cb(lv_timer_t *timer)
{
    ui_link_state_t link = s_link_probe();
    bool linked = link.usb || link.ble;
    s_bridge_linked = linked;
    s_dash.host_linked = link.usb;
    s_dash.ble_connected = link.ble;
    if (s_clock_synced) {
        clock_format();
    }
    agent_dashboard_update(&s_dash);
    robot_base_update();
    needs_user_update();
    screen_power_update(link);
    if (!linked && s_modal_visible) {
        modal_hide(); /* the bridge releases the request to the terminal on its side */
    }
    if (!s_face_mode && !s_modal_visible && lv_obj_is_hidden(s_pairing) && !settings_screen_is_open() &&
        lv_tick_elaps(s_last_activity) >= FACE_IDLE_MS) {
        face_mode_set(true);
    }
}

/* ---------- public API ---------- */

void ui_init(ui_decision_cb_t on_decision, ui_link_probe_t link_probe, const settings_values_t *settings,
             settings_change_cb_t on_settings)
{
    s_on_decision = on_decision;
    s_link_probe = link_probe;

    bsp_display_lock(0);
    lv_obj_set_style_bg_color(lv_screen_active(), lv_color_black(), 0);
    agent_dashboard_create(lv_screen_active());
    /* Nothing is known until the bridge links and syncs: usage, cost and battery show "--". */
    s_dash = (agent_dashboard_data_t){
        .claude_usage_percent = -1,
        .codex_usage_percent = -1,
        .claude_weekly_percent = -1,
        .codex_weekly_percent = -1,
        .daily_cost = -1,
        .battery_percent = -1,
        .status_text = "Waiting for connection...",
        .status_tone = AGENT_DASHBOARD_TONE_NEUTRAL,
    };
    agent_dashboard_update(&s_dash);
    s_last_activity = lv_tick_get();
    s_last_powered = s_last_activity;
    robot_base_update();
    touch_catcher_create();
    settings_screen_create(lv_layer_top(), settings, on_settings, activity_note);
    modal_create();
    pairing_create();
    wake_layer_create();
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

void ui_set_battery(int percent, bool charging, bool discharging)
{
    bsp_display_lock(0);
    s_battery_discharging = discharging;
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
    activity_note();
    s_attention_since = kind == UI_EVENT_ATTENTION ? LV_MAX(lv_tick_get(), 1) : 0;
    needs_user_update();
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
    activity_note();
    robot_base_update();
    if (busy) {
        s_attention_since = 0; /* the agent got its input */
        needs_user_update();
        /* A new turn replaces any reaction still showing (the base may already be busy). */
        robot_face_react(ROBOT_MOOD_BUSY, ROBOT_LOOK_CENTER, 0);
    }
    bsp_display_unlock();
}

void ui_show_speech_face(const char *mood, const char *look, uint32_t hold_ms)
{
    static const struct {
        const char *name;
        robot_mood_t mood;
    } moods[] = {
        {"happy", ROBOT_MOOD_HAPPY},
        {"surprised", ROBOT_MOOD_SURPRISED},
        {"sleep", ROBOT_MOOD_SLEEP},
        {"idle", ROBOT_MOOD_IDLE},
    };
    robot_look_t gaze = look == NULL ? ROBOT_LOOK_CENTER
                        : strcmp(look, "left") == 0 ? ROBOT_LOOK_LEFT
                        : strcmp(look, "right") == 0 ? ROBOT_LOOK_RIGHT
                                                     : ROBOT_LOOK_CENTER;
    for (size_t i = 0; i < sizeof(moods) / sizeof(moods[0]); i++) {
        if (mood != NULL && strcmp(mood, moods[i].name) == 0) {
            bsp_display_lock(0);
            robot_face_react(moods[i].mood, gaze, hold_ms);
            bsp_display_unlock();
            return;
        }
    }
}

void ui_show_confirm(uint32_t id, const char *source, const char *tool, const char *summary, ui_risk_t risk)
{
    bsp_display_lock(0);
    activity_note();
    modal_show(id, agent_index(source) == AGENT_CODEX ? &img_logo_codex : &img_logo_claude, tool, summary, risk);
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

void ui_show_pairing(bool active, uint32_t passkey)
{
    bsp_display_lock(0);
    if (active) {
        char code[8];
        snprintf(code, sizeof(code), "%06lu", (unsigned long)passkey);
        lv_label_set_text(s_pairing_code, code);
        settings_screen_close();
        activity_note();
    }
    lv_obj_set_hidden(s_pairing, !active);
    bsp_display_unlock();
}

/* Faces the body itself causes, as opposed to an agent's reactions. */
static bool body_mood(robot_mood_t mood)
{
    return mood == ROBOT_MOOD_SURPRISED || mood == ROBOT_MOOD_LEAN || mood == ROBOT_MOOD_DIZZY ||
           mood == ROBOT_MOOD_STARGAZE;
}

/*
 * The body may take over the idle or busy face and any body face, but an agent's reaction
 * (happy, attention, error, a speech mood) and sleep win over it.
 */
static bool body_may_react(void)
{
    robot_mood_t shown = robot_face_mood();
    return robot_face_is_reacting() ? body_mood(shown) : shown == ROBOT_MOOD_IDLE || shown == ROBOT_MOOD_BUSY;
}

/* True once the tilts have swung back and forth ROCK_SWINGS times in a row. */
static bool rock_note(int dir)
{
    bool swing = dir != s_rock_dir && s_rock_at != 0 && lv_tick_elaps(s_rock_at) < ROCK_GAP_MS;
    s_rock_swings = swing ? s_rock_swings + 1 : 1;
    s_rock_dir = dir;
    s_rock_at = lv_tick_get();
    if (s_rock_swings < ROCK_SWINGS) {
        return false;
    }
    s_rock_swings = 0;
    return true;
}

static void react_end_if(robot_mood_t mood)
{
    if (robot_face_is_reacting() && robot_face_mood() == mood) {
        robot_face_react_end();
    }
}

void ui_body_event(ui_body_event_t event)
{
    bsp_display_lock(0);
    /*
     * Counts as activity, but keeps the fullscreen face: picking the robot up should make
     * the big face react, not swap it for the small dashboard head.
     */
    s_last_activity = lv_tick_get();
    robot_base_update(); /* wakes a dozing robot first; a base change ends any reaction */
    robot_mood_t shown = robot_face_mood();
    switch (event) {
    case UI_BODY_MOVED:
        /* Tilting, shaking or laying it down also jolts the sensor: keep those faces. */
        if (!robot_face_is_reacting() || !body_mood(shown) || shown == ROBOT_MOOD_SURPRISED) {
            robot_face_react(ROBOT_MOOD_SURPRISED, ROBOT_LOOK_CENTER, SURPRISED_MS);
        }
        break;
    case UI_BODY_TILT_LEFT:
    case UI_BODY_TILT_RIGHT: {
        int dir = event == UI_BODY_TILT_LEFT ? -1 : 1;
        if (!body_may_react()) {
            rock_note(dir);
        } else if (rock_note(dir)) {
            robot_face_react(ROBOT_MOOD_HAPPY, ROBOT_LOOK_CENTER, ROCK_HAPPY_MS);
        } else {
            robot_face_react(ROBOT_MOOD_LEAN, dir < 0 ? ROBOT_LOOK_LEFT : ROBOT_LOOK_RIGHT, TILT_LEAN_MAX_MS);
        }
        break;
    }
    case UI_BODY_LEVEL:
        react_end_if(ROBOT_MOOD_LEAN);
        break;
    case UI_BODY_SHAKEN:
        if (body_may_react()) {
            robot_face_react(ROBOT_MOOD_DIZZY, ROBOT_LOOK_CENTER, DIZZY_MS);
        }
        break;
    case UI_BODY_ON_BACK:
        /* Until it is picked up again, or until it dozes off under the stars. */
        if (body_may_react()) {
            robot_face_react(ROBOT_MOOD_STARGAZE, ROBOT_LOOK_CENTER, 0);
        }
        break;
    case UI_BODY_OFF_BACK:
        react_end_if(ROBOT_MOOD_STARGAZE);
        break;
    }
    bsp_display_unlock();
}

void ui_set_quiet(bool quiet)
{
    bsp_display_lock(0);
    snprintf(s_dash.status_text, sizeof(s_dash.status_text), "%s", quiet ? "Quiet mode" : "Sound on");
    s_dash.status_tone = AGENT_DASHBOARD_TONE_NEUTRAL;
    agent_dashboard_update(&s_dash);
    activity_note();
    robot_base_update();
    robot_face_react(quiet ? ROBOT_MOOD_SLEEP : ROBOT_MOOD_HAPPY, ROBOT_LOOK_CENTER, QUIET_REACT_MS);
    bsp_display_unlock();
}

/* LVGL task: hands the latest gravity to the face, whose eyes roll toward it. */
static void gravity_timer_cb(lv_timer_t *timer)
{
    int lateral_mg;
    int down_mg;
    if (s_gravity_probe != NULL && s_gravity_probe(&lateral_mg, &down_mg)) {
        robot_face_set_gravity(lateral_mg, down_mg);
    }
}

void ui_set_gravity_probe(ui_gravity_probe_t probe)
{
    bsp_display_lock(0);
    if (s_gravity_probe == NULL) {
        lv_timer_create(gravity_timer_cb, GRAVITY_POLL_MS, NULL);
    }
    s_gravity_probe = probe;
    bsp_display_unlock();
}

void ui_set_needs_user_cb(ui_needs_user_cb_t cb)
{
    bsp_display_lock(0);
    s_on_needs_user = cb;
    bsp_display_unlock();
}

void ui_set_screen_off_cb(ui_screen_off_cb_t cb)
{
    bsp_display_lock(0);
    s_on_screen_off = cb;
    bsp_display_unlock();
}
