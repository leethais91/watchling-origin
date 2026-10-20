#include "settings_screen.h"

#include <stdio.h>
#include "assets/dashboard_assets.h"

/*
 * Colours follow the dashboard: brightness takes Claude's orange on the left, volume Codex's
 * teal on the right, so the screen mirrors the usage gauges it replaces.
 */
#define COLOR_BG             0x0B0F19
#define COLOR_ARC_TRACK      0x182638
#define COLOR_BRIGHTNESS     0xFF854D
#define COLOR_BRIGHTNESS_HI  0xFFD3BD
#define COLOR_VOLUME         0x10DFA8
#define COLOR_VOLUME_HI      0xB8FFEB
#define COLOR_ELECTRIC_BLUE  0x00BFFF
#define COLOR_RIM_OUTER      0x1760A0
#define COLOR_DRAWER_BG      0x111A2B
#define COLOR_TEXT_PRIMARY   0xF4F7FC
#define COLOR_TEXT_SECONDARY 0x94A3B8
#define COLOR_SWITCH_OFF     0x243247

/*
 * Pull indicator: a drawer slides down with the finger and the ring around the gear fills.
 * PULL_OPEN_PX of travel (about half the screen, counted from the touch-down) opens settings.
 */
#define PULL_OPEN_PX         150
#define DRAWER_W             196
#define DRAWER_H             112
#define DRAWER_TOP_Y         22  /* drawer top once the pull reaches the threshold */
#define DRAWER_RADIUS        24
#define PULL_RING_DIAMETER   54
#define PULL_RING_WIDTH      4
#define PULL_SCRIM_MAX_OPA   LV_OPA_60
#define SPRING_BACK_MS       180

/*
 * Screen layout (offsets from the centre). Arcs hug the edge like the usage gauges:
 * LVGL angles start at 3 o'clock and grow clockwise, so 130..230 is the left edge and
 * 310..50 the right one (reverse mode: the value grows from the bottom on both sides).
 */
#define SCREEN_SLIDE_MS      220
#define ARC_DIAMETER         322
#define ARC_TRACK_WIDTH      16
#define ARC_VALUE_WIDTH      14
#define ARC_TOUCH_EXT_PX     14
#define BRIGHTNESS_ARC_START 130
#define BRIGHTNESS_ARC_END   230
#define VOLUME_ARC_START     310
#define VOLUME_ARC_END       50
#define SIDE_X               108
#define SIDE_ICON_Y          -14
#define SIDE_VALUE_Y         18
#define GEAR_Y               -128
#define TITLE_Y              -100
/* Voice and Bluetooth stack in the middle column: a label, then its switch. */
#define VOICE_LABEL_Y        -60
#define VOICE_SWITCH_Y       -26
#define BLE_LABEL_Y          16
#define BLE_SWITCH_Y         50
#define SWITCH_W             76
#define SWITCH_H             36
#define HINT_ICON_Y          104
#define HINT_Y               126
#define SUN_RAY_COUNT        8
#define SUN_CORE_PX          10
#define SUN_RAY_PX           3
#define SUN_RAY_R            10
#define IDLE_CLOSE_MS        (15 * 1000)
#define IDLE_POLL_MS         1000

static settings_values_t s_values;
static settings_change_cb_t s_on_change;
static settings_closed_cb_t s_on_closed;
static bool s_open;

static lv_obj_t *s_scrim;
static lv_obj_t *s_drawer;
static lv_obj_t *s_pull_ring;
static lv_obj_t *s_pull_text;
static int32_t s_pull_shown; /* distance the indicator shows right now (spring-back included) */

static lv_obj_t *s_screen;
static lv_obj_t *s_brightness_value;
static lv_obj_t *s_volume_value;

static lv_obj_t *label_create(lv_obj_t *parent, const lv_font_t *font, uint32_t color, int32_t x, int32_t y,
                              const char *text)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_label_set_text(label, text);
    lv_obj_align(label, LV_ALIGN_CENTER, x, y);
    return label;
}

/* ---------- pull indicator ---------- */

static void pull_indicator_create(lv_obj_t *parent)
{
    /* Dims the dashboard as the pull goes on. Never clickable: the touch stays with the catcher. */
    s_scrim = lv_obj_create(parent);
    lv_obj_remove_style_all(s_scrim);
    lv_obj_set_size(s_scrim, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(s_scrim, lv_color_black(), 0);
    lv_obj_set_clickable(s_scrim, false);
    lv_obj_set_hidden(s_scrim, true);

    s_drawer = lv_obj_create(parent);
    lv_obj_remove_style_all(s_drawer);
    lv_obj_set_size(s_drawer, DRAWER_W, DRAWER_H);
    lv_obj_set_style_radius(s_drawer, DRAWER_RADIUS, 0);
    lv_obj_set_style_bg_color(s_drawer, lv_color_hex(COLOR_DRAWER_BG), 0);
    lv_obj_set_style_bg_opa(s_drawer, LV_OPA_90, 0);
    lv_obj_set_style_border_width(s_drawer, 1, 0);
    lv_obj_set_style_border_color(s_drawer, lv_color_hex(COLOR_ELECTRIC_BLUE), 0);
    lv_obj_set_style_border_opa(s_drawer, LV_OPA_40, 0);
    lv_obj_set_clickable(s_drawer, false);
    lv_obj_set_hidden(s_drawer, true);

    s_pull_ring = lv_arc_create(s_drawer);
    lv_obj_remove_style_all(s_pull_ring);
    lv_obj_set_size(s_pull_ring, PULL_RING_DIAMETER, PULL_RING_DIAMETER);
    lv_obj_align(s_pull_ring, LV_ALIGN_TOP_MID, 0, 14);
    lv_obj_set_clickable(s_pull_ring, false);
    lv_arc_set_rotation(s_pull_ring, 270);
    lv_arc_set_bg_angles(s_pull_ring, 0, 360);
    lv_arc_set_range(s_pull_ring, 0, PULL_OPEN_PX);
    lv_obj_set_style_arc_width(s_pull_ring, PULL_RING_WIDTH, LV_PART_MAIN);
    lv_obj_set_style_arc_color(s_pull_ring, lv_color_hex(COLOR_ARC_TRACK), LV_PART_MAIN);
    lv_obj_set_style_arc_width(s_pull_ring, PULL_RING_WIDTH, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(s_pull_ring, lv_color_hex(COLOR_ELECTRIC_BLUE), LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(s_pull_ring, true, LV_PART_INDICATOR);

    lv_obj_t *gear = lv_label_create(s_pull_ring);
    lv_obj_set_style_text_font(gear, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(gear, lv_color_hex(COLOR_ELECTRIC_BLUE), 0);
    lv_label_set_text(gear, LV_SYMBOL_SETTINGS);
    lv_obj_center(gear);

    s_pull_text = lv_label_create(s_drawer);
    lv_obj_set_style_text_font(s_pull_text, &font_mono_15, 0);
    lv_obj_set_style_text_color(s_pull_text, lv_color_hex(COLOR_TEXT_SECONDARY), 0);
    lv_obj_align(s_pull_text, LV_ALIGN_BOTTOM_MID, 0, -12);
}

/* Draws the indicator for a pull of `distance` px; 0 hides it. */
static void pull_render(int32_t distance)
{
    s_pull_shown = distance;
    bool visible = distance > 0;
    lv_obj_set_hidden(s_scrim, !visible);
    lv_obj_set_hidden(s_drawer, !visible);
    if (!visible) {
        return;
    }
    int32_t travel = LV_MIN(distance, PULL_OPEN_PX);
    bool ready = distance >= PULL_OPEN_PX;
    lv_obj_set_style_bg_opa(s_scrim, (lv_opa_t)(PULL_SCRIM_MAX_OPA * travel / PULL_OPEN_PX), 0);
    lv_obj_align(s_drawer, LV_ALIGN_TOP_MID, 0, DRAWER_TOP_Y - DRAWER_H + DRAWER_H * travel / PULL_OPEN_PX);
    lv_arc_set_value(s_pull_ring, travel);
    lv_label_set_text(s_pull_text, ready ? "Release to open" : "Keep pulling");
    lv_obj_set_style_text_color(s_pull_text, lv_color_hex(ready ? COLOR_ELECTRIC_BLUE : COLOR_TEXT_SECONDARY), 0);
}

static void pull_anim_cb(void *var, int32_t value)
{
    LV_UNUSED(var);
    pull_render(value);
}

/* ---------- settings screen ---------- */

static void screen_y_anim_cb(void *obj, int32_t value)
{
    lv_obj_set_y(obj, value);
}

static void screen_hidden_cb(lv_anim_t *a)
{
    lv_obj_set_hidden(a->var, true);
}

/* Slides the screen in from above, or back up and hides it at the end. */
static void screen_slide(bool in)
{
    int32_t height = lv_obj_get_height(lv_obj_get_parent(s_screen));
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_screen);
    lv_anim_set_exec_cb(&a, screen_y_anim_cb);
    lv_anim_set_duration(&a, SCREEN_SLIDE_MS);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    if (in) {
        lv_anim_set_values(&a, -height, 0);
        lv_obj_set_hidden(s_screen, false);
    } else {
        lv_anim_set_values(&a, lv_obj_get_y(s_screen), -height);
        lv_anim_set_completed_cb(&a, screen_hidden_cb);
    }
    lv_anim_delete(s_screen, screen_y_anim_cb);
    lv_anim_start(&a);
}

static void value_text_set(lv_obj_t *label, int percent)
{
    char text[8];
    snprintf(text, sizeof(text), "%d%%", percent);
    lv_label_set_text(label, text);
}

static void arc_event_cb(lv_event_t *e)
{
    lv_obj_t *arc = lv_event_get_target(e);
    settings_field_t field = (settings_field_t)(intptr_t)lv_event_get_user_data(e);
    int value = lv_arc_get_value(arc);
    if (field == SETTINGS_FIELD_BRIGHTNESS) {
        s_values.brightness = value;
        value_text_set(s_brightness_value, value);
    } else {
        s_values.volume = value;
        value_text_set(s_volume_value, value);
    }
    s_on_change(field, &s_values, lv_event_get_code(e) == LV_EVENT_RELEASED);
}

/*
 * An arc slider along the edge. Both arcs span the whole panel, so each must hit-test its own
 * ring (lv_arc's advanced hit test) or the one on top takes every touch. Drags stay with the
 * arc, so adjusting it never reads as the swipe that closes the screen.
 */
static lv_obj_t *edge_arc_create(lv_obj_t *parent, uint16_t start, uint16_t end, bool reverse, int min, int value,
                                 uint32_t color, uint32_t knob_color, settings_field_t field)
{
    lv_obj_t *arc = lv_arc_create(parent);
    lv_obj_remove_style_all(arc);
    lv_obj_set_size(arc, ARC_DIAMETER, ARC_DIAMETER);
    lv_obj_center(arc);
    lv_obj_set_ext_click_area(arc, ARC_TOUCH_EXT_PX);
    lv_obj_set_adv_hittest(arc, true);
    lv_obj_set_gesture_bubble(arc, false);
    lv_arc_set_mode(arc, reverse ? LV_ARC_MODE_REVERSE : LV_ARC_MODE_NORMAL);
    lv_arc_set_bg_angles(arc, start, end);
    lv_arc_set_range(arc, min, 100);
    lv_arc_set_value(arc, value);

    lv_obj_set_style_arc_width(arc, ARC_TRACK_WIDTH, LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc, lv_color_hex(COLOR_ARC_TRACK), LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(arc, true, LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, ARC_VALUE_WIDTH, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc, lv_color_hex(color), LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(arc, true, LV_PART_INDICATOR);
    /* Track and value strokes are drawn inward from the edge; inset the value to centre it. */
    lv_obj_set_style_pad_all(arc, (ARC_TRACK_WIDTH - ARC_VALUE_WIDTH) / 2, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(arc, lv_color_hex(knob_color), LV_PART_KNOB);
    lv_obj_set_style_bg_opa(arc, LV_OPA_COVER, LV_PART_KNOB);
    lv_obj_set_style_radius(arc, LV_RADIUS_CIRCLE, LV_PART_KNOB);
    lv_obj_set_style_pad_all(arc, 3, LV_PART_KNOB);
    lv_obj_set_style_shadow_width(arc, 12, LV_PART_KNOB);
    lv_obj_set_style_shadow_color(arc, lv_color_hex(color), LV_PART_KNOB);

    lv_obj_add_event_cb(arc, arc_event_cb, LV_EVENT_VALUE_CHANGED, (void *)(intptr_t)field);
    lv_obj_add_event_cb(arc, arc_event_cb, LV_EVENT_RELEASED, (void *)(intptr_t)field);
    return arc;
}

/* A small sun (core plus rays) for brightness; Montserrat's symbols have none. */
static void sun_icon_create(lv_obj_t *parent, int32_t x, int32_t y)
{
    static lv_style_t dot;
    if (dot.prop_cnt == 0) {
        lv_style_init(&dot);
        lv_style_set_radius(&dot, LV_RADIUS_CIRCLE);
        lv_style_set_bg_color(&dot, lv_color_hex(COLOR_BRIGHTNESS));
        lv_style_set_bg_opa(&dot, LV_OPA_COVER);
    }
    lv_obj_t *core = lv_obj_create(parent);
    lv_obj_remove_style_all(core);
    lv_obj_add_style(core, &dot, 0);
    lv_obj_set_size(core, SUN_CORE_PX, SUN_CORE_PX);
    lv_obj_align(core, LV_ALIGN_CENTER, x, y);
    for (int i = 0; i < SUN_RAY_COUNT; i++) {
        int32_t angle = i * 360 / SUN_RAY_COUNT;
        lv_obj_t *ray = lv_obj_create(parent);
        lv_obj_remove_style_all(ray);
        lv_obj_add_style(ray, &dot, 0);
        lv_obj_set_size(ray, SUN_RAY_PX, SUN_RAY_PX);
        lv_obj_align(ray, LV_ALIGN_CENTER, x + SUN_RAY_R * lv_trigo_cos(angle) / LV_TRIGO_SIN_MAX,
                     y + SUN_RAY_R * lv_trigo_sin(angle) / LV_TRIGO_SIN_MAX);
    }
}

static void switch_cb(lv_event_t *e)
{
    settings_field_t field = (settings_field_t)(intptr_t)lv_event_get_user_data(e);
    bool on = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
    if (field == SETTINGS_FIELD_VOICE) {
        s_values.voice = on;
    } else {
        s_values.ble = on;
    }
    s_on_change(field, &s_values, true);
}

static void switch_create(lv_obj_t *parent, int32_t y, bool on, settings_field_t field)
{
    lv_obj_t *sw = lv_switch_create(parent);
    lv_obj_set_size(sw, SWITCH_W, SWITCH_H);
    lv_obj_align(sw, LV_ALIGN_CENTER, 0, y);
    lv_obj_set_gesture_bubble(sw, false);
    lv_obj_set_style_bg_color(sw, lv_color_hex(COLOR_SWITCH_OFF), LV_PART_MAIN);
    lv_obj_set_style_bg_color(sw, lv_color_hex(COLOR_ELECTRIC_BLUE), LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(sw, lv_color_hex(COLOR_TEXT_PRIMARY), LV_PART_KNOB);
    lv_obj_set_style_shadow_width(sw, 14, LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_shadow_color(sw, lv_color_hex(COLOR_ELECTRIC_BLUE), LV_PART_INDICATOR | LV_STATE_CHECKED);
    if (on) {
        lv_obj_add_state(sw, LV_STATE_CHECKED);
    }
    lv_obj_add_event_cb(sw, switch_cb, LV_EVENT_VALUE_CHANGED, (void *)(intptr_t)field);
}

static void close_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_GESTURE && lv_indev_get_gesture_dir(lv_indev_active()) != LV_DIR_TOP) {
        return;
    }
    settings_screen_close();
}

static void idle_close_timer_cb(lv_timer_t *timer)
{
    LV_UNUSED(timer);
    if (s_open && lv_display_get_inactive_time(NULL) >= IDLE_CLOSE_MS) {
        settings_screen_close();
    }
}

static void screen_create(lv_obj_t *parent)
{
    s_screen = lv_obj_create(parent);
    lv_obj_remove_style_all(s_screen);
    lv_obj_set_size(s_screen, lv_pct(100), lv_pct(100));
    lv_obj_set_style_radius(s_screen, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_screen, lv_color_hex(COLOR_BG), 0);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_screen, 2, 0);
    lv_obj_set_style_border_color(s_screen, lv_color_hex(COLOR_RIM_OUTER), 0);
    lv_obj_set_scrollable(s_screen, false);
    /* Swipes on the background (not on the arcs or the switch) arrive here. */
    lv_obj_add_event_cb(s_screen, close_cb, LV_EVENT_GESTURE, NULL);

    label_create(s_screen, &lv_font_montserrat_20, COLOR_ELECTRIC_BLUE, 0, GEAR_Y, LV_SYMBOL_SETTINGS);
    label_create(s_screen, &lv_font_montserrat_20, COLOR_TEXT_PRIMARY, 0, TITLE_Y, "Settings");

    edge_arc_create(s_screen, BRIGHTNESS_ARC_START, BRIGHTNESS_ARC_END, false, SETTINGS_BRIGHTNESS_MIN,
                    s_values.brightness, COLOR_BRIGHTNESS, COLOR_BRIGHTNESS_HI, SETTINGS_FIELD_BRIGHTNESS);
    edge_arc_create(s_screen, VOLUME_ARC_START, VOLUME_ARC_END, true, 0, s_values.volume, COLOR_VOLUME,
                    COLOR_VOLUME_HI, SETTINGS_FIELD_VOLUME);
    sun_icon_create(s_screen, -SIDE_X, SIDE_ICON_Y);
    label_create(s_screen, &lv_font_montserrat_20, COLOR_VOLUME, SIDE_X, SIDE_ICON_Y, LV_SYMBOL_VOLUME_MAX);
    s_brightness_value = label_create(s_screen, &font_dash_bold_20, COLOR_BRIGHTNESS, -SIDE_X, SIDE_VALUE_Y, "");
    s_volume_value = label_create(s_screen, &font_dash_bold_20, COLOR_VOLUME, SIDE_X, SIDE_VALUE_Y, "");
    value_text_set(s_brightness_value, s_values.brightness);
    value_text_set(s_volume_value, s_values.volume);

    label_create(s_screen, &lv_font_montserrat_16, COLOR_TEXT_PRIMARY, 0, VOICE_LABEL_Y, "Voice (TTS)");
    switch_create(s_screen, VOICE_SWITCH_Y, s_values.voice, SETTINGS_FIELD_VOICE);
    label_create(s_screen, &lv_font_montserrat_16, COLOR_TEXT_PRIMARY, 0, BLE_LABEL_Y, LV_SYMBOL_BLUETOOTH " Bluetooth");
    switch_create(s_screen, BLE_SWITCH_Y, s_values.ble, SETTINGS_FIELD_BLE);

    /* The hint also closes on a tap, for anyone who does not try the swipe. */
    lv_obj_t *hint_icon = label_create(s_screen, &lv_font_montserrat_16, COLOR_TEXT_SECONDARY, 0, HINT_ICON_Y,
                                       LV_SYMBOL_UP);
    lv_obj_t *hint = label_create(s_screen, &font_mono_15, COLOR_TEXT_SECONDARY, 0, HINT_Y, "Swipe up to close");
    lv_obj_t *targets[] = {hint_icon, hint};
    for (size_t i = 0; i < sizeof(targets) / sizeof(targets[0]); i++) {
        lv_obj_set_clickable(targets[i], true);
        lv_obj_set_ext_click_area(targets[i], 10);
        lv_obj_add_event_cb(targets[i], close_cb, LV_EVENT_CLICKED, NULL);
    }

    lv_obj_set_hidden(s_screen, true);
    lv_timer_create(idle_close_timer_cb, IDLE_POLL_MS, NULL);
}

/* ---------- public API ---------- */

void settings_screen_create(lv_obj_t *parent, const settings_values_t *initial, settings_change_cb_t on_change,
                            settings_closed_cb_t on_closed)
{
    s_values = *initial;
    s_on_change = on_change;
    s_on_closed = on_closed;
    pull_indicator_create(parent);
    screen_create(parent);
}

void settings_screen_pull(int32_t distance)
{
    lv_anim_delete(s_drawer, pull_anim_cb);
    pull_render(LV_MAX(distance, 0));
}

void settings_screen_pull_release(int32_t distance)
{
    if (distance >= PULL_OPEN_PX) {
        pull_render(0);
        s_open = true;
        screen_slide(true);
        return;
    }
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_drawer);
    lv_anim_set_exec_cb(&a, pull_anim_cb);
    lv_anim_set_values(&a, s_pull_shown, 0);
    lv_anim_set_duration(&a, SPRING_BACK_MS);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_start(&a);
}

bool settings_screen_is_open(void)
{
    return s_open;
}

void settings_screen_close(void)
{
    if (!s_open) {
        return;
    }
    s_open = false;
    screen_slide(false);
    s_on_closed();
}
