#include "ui.h"

#include <stdio.h>
#include <string.h>
#include "bsp/esp-bsp.h"
#include "lvgl.h"

/*
 * Layout rules for the round panel: nothing in the square corners. The usage arcs hug
 * the edge (left = Claude, right = Codex) and everything else stays inside a ~150px
 * radius from the center.
 */

#define COLOR_BG          0x0B0F19
#define COLOR_ARC_TRACK   0x1F2937
#define COLOR_MUTED       0x9CA3AF
#define COLOR_CLAUDE      0xD97757
#define COLOR_CODEX       0x10A37F
#define COLOR_OK          0x22C55E
#define COLOR_WARN        0xF59E0B
#define COLOR_DANGER      0xEF4444

#define ARC_WIDTH_PX      10
#define MODAL_RING_PX     8
#define BUTTON_W          112
#define BUTTON_H          60
#define BUTTON_OFFSET_X   62
#define BUTTON_OFFSET_Y   92
#define RING_PULSE_MS     500
#define LINK_POLL_MS      1000

static ui_decision_cb_t s_on_decision;
static ui_link_probe_t s_link_probe;

static lv_obj_t *s_claude_arc;
static lv_obj_t *s_codex_arc;
static lv_obj_t *s_claude_label;
static lv_obj_t *s_codex_label;
static lv_obj_t *s_cost_label;
static lv_obj_t *s_status_label;
static lv_obj_t *s_link_label;

static lv_obj_t *s_modal;
static lv_obj_t *s_modal_title;
static lv_obj_t *s_modal_tool;
static lv_obj_t *s_modal_summary;
static uint32_t s_modal_id;
static bool s_modal_visible;

/* ---------- dashboard ---------- */

static lv_obj_t *usage_arc_create(lv_obj_t *parent, uint16_t start_deg, uint16_t end_deg,
                                  uint32_t color, lv_arc_mode_t mode)
{
    /* LVGL angles start at 3 o'clock and grow clockwise: 135..225 is the left edge. */
    lv_obj_t *arc = lv_arc_create(parent);
    lv_obj_set_size(arc, BSP_LCD_H_RES, BSP_LCD_V_RES);
    lv_obj_center(arc);
    lv_arc_set_bg_angles(arc, start_deg, end_deg);
    lv_arc_set_mode(arc, mode);
    lv_arc_set_range(arc, 0, 100);
    lv_arc_set_value(arc, 0);
    lv_obj_remove_style(arc, NULL, LV_PART_KNOB);
    lv_obj_set_clickable(arc, false);
    lv_obj_set_style_arc_width(arc, ARC_WIDTH_PX, LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, ARC_WIDTH_PX, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc, lv_color_hex(COLOR_ARC_TRACK), LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc, lv_color_hex(color), LV_PART_INDICATOR);
    return arc;
}

static lv_obj_t *side_label_create(lv_obj_t *parent, lv_align_t align, int32_t x, uint32_t color)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(label, align, x, 0);
    return label;
}

static void set_side_label(lv_obj_t *label, const char *name, int h5, int d7)
{
    char h5_text[12] = "--";
    char d7_text[12] = "--";
    if (h5 >= 0) {
        snprintf(h5_text, sizeof(h5_text), "%d%%", h5);
    }
    if (d7 >= 0) {
        snprintf(d7_text, sizeof(d7_text), "%d%%", d7);
    }
    lv_label_set_text_fmt(label, "%s\n%s\n7d %s", name, h5_text, d7_text);
}

static void dashboard_create(lv_obj_t *scr)
{
    lv_obj_set_style_bg_color(scr, lv_color_hex(COLOR_BG), 0);
    lv_obj_set_style_text_color(scr, lv_color_white(), 0);

    /* Both arcs fill bottom-up; the right one runs counter-clockwise from 45°. */
    s_claude_arc = usage_arc_create(scr, 135, 225, COLOR_CLAUDE, LV_ARC_MODE_NORMAL);
    s_codex_arc = usage_arc_create(scr, 315, 45, COLOR_CODEX, LV_ARC_MODE_REVERSE);

    s_claude_label = side_label_create(scr, LV_ALIGN_LEFT_MID, 24, COLOR_CLAUDE);
    s_codex_label = side_label_create(scr, LV_ALIGN_RIGHT_MID, -24, COLOR_CODEX);
    set_side_label(s_claude_label, "Claude", -1, -1);
    set_side_label(s_codex_label, "Codex", -1, -1);

    s_link_label = lv_label_create(scr);
    lv_obj_align(s_link_label, LV_ALIGN_TOP_MID, 0, 40);

    s_cost_label = lv_label_create(scr);
    lv_obj_set_style_text_font(s_cost_label, &lv_font_montserrat_28, 0);
    lv_label_set_text(s_cost_label, "$--");
    lv_obj_align(s_cost_label, LV_ALIGN_CENTER, 0, -38);

    lv_obj_t *caption = lv_label_create(scr);
    lv_obj_set_style_text_color(caption, lv_color_hex(COLOR_MUTED), 0);
    lv_label_set_text(caption, "Claude today");
    lv_obj_align(caption, LV_ALIGN_CENTER, 0, -10);

    s_status_label = lv_label_create(scr);
    lv_obj_set_width(s_status_label, 170);
    lv_obj_set_height(s_status_label, 3 * 18);
    lv_label_set_long_mode(s_status_label, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_style_text_align(s_status_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(s_status_label, lv_color_hex(COLOR_MUTED), 0);
    lv_label_set_text(s_status_label, "Idle");
    lv_obj_align(s_status_label, LV_ALIGN_CENTER, 0, 48);
}

static void set_link_state(bool linked)
{
    lv_label_set_text(s_link_label, linked ? LV_SYMBOL_USB " linked" : LV_SYMBOL_USB " no bridge");
    lv_obj_set_style_text_color(s_link_label, lv_color_hex(linked ? COLOR_OK : COLOR_MUTED), 0);
}

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
    lv_obj_set_style_radius(btn, BUTTON_H / 2, 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(color), 0);
    lv_obj_align(btn, LV_ALIGN_CENTER, x, BUTTON_OFFSET_Y);
    lv_obj_add_event_cb(btn, decision_button_cb, LV_EVENT_CLICKED, (void *)(intptr_t)approve);

    lv_obj_t *label = lv_label_create(btn);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_20, 0);
    lv_label_set_text(label, text);
    lv_obj_center(label);
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
    lv_obj_set_size(box, 260, 100);
    lv_obj_align(box, LV_ALIGN_CENTER, 0, -8);
    lv_obj_set_style_bg_opa(box, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(box, 0, 0);
    lv_obj_set_style_pad_all(box, 4, 0);
    lv_obj_set_scrollbar_mode(box, LV_SCROLLBAR_MODE_AUTO);

    s_modal_summary = lv_label_create(box);
    lv_obj_set_width(s_modal_summary, lv_pct(100));
    lv_label_set_long_mode(s_modal_summary, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_text_font(s_modal_summary, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_align(s_modal_summary, LV_TEXT_ALIGN_CENTER, 0);

    decision_button_create(s_modal, "DENY", COLOR_DANGER, -BUTTON_OFFSET_X, false);
    decision_button_create(s_modal, "APPROVE", COLOR_OK, BUTTON_OFFSET_X, true);

    lv_obj_set_hidden(s_modal, true);
}

static void modal_show(uint32_t id, const char *tool, const char *summary, ui_risk_t risk, const char *project)
{
    uint32_t ring = risk == UI_RISK_HIGH ? COLOR_DANGER : COLOR_WARN;
    s_modal_id = id;
    lv_obj_set_style_border_color(s_modal, lv_color_hex(ring), 0);
    lv_obj_set_style_text_color(s_modal_tool, lv_color_hex(ring), 0);
    lv_label_set_text_fmt(s_modal_title, "Claude%s%s", project[0] ? " - " : "", project);
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

static void link_timer_cb(lv_timer_t *timer)
{
    bool linked = s_link_probe();
    set_link_state(linked);
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
    dashboard_create(lv_screen_active());
    modal_create();
    set_link_state(false);
    lv_timer_create(link_timer_cb, LINK_POLL_MS, NULL);
    bsp_display_unlock();
}

void ui_set_usage(const ui_usage_t *usage)
{
    bsp_display_lock(0);
    lv_arc_set_value(s_claude_arc, usage->claude_h5 > 0 ? usage->claude_h5 : 0);
    lv_arc_set_value(s_codex_arc, usage->codex_h5 > 0 ? usage->codex_h5 : 0);
    set_side_label(s_claude_label, "Claude", usage->claude_h5, usage->claude_d7);
    set_side_label(s_codex_label, "Codex", usage->codex_h5, usage->codex_d7);
    if (usage->cost_today >= 0) {
        lv_label_set_text_fmt(s_cost_label, "$%.2f", (double)usage->cost_today);
    } else {
        lv_label_set_text(s_cost_label, "$--");
    }
    bsp_display_unlock();
}

void ui_show_event(const char *source, ui_event_kind_t kind, const char *text)
{
    static const uint32_t colors[] = {
        [UI_EVENT_DONE] = COLOR_OK,
        [UI_EVENT_ATTENTION] = COLOR_WARN,
        [UI_EVENT_ERROR] = COLOR_DANGER,
    };
    const char *name = strcmp(source, "codex") == 0 ? "Codex" : "Claude";

    bsp_display_lock(0);
    lv_obj_set_style_text_color(s_status_label, lv_color_hex(colors[kind]), 0);
    lv_label_set_text_fmt(s_status_label, "%s: %s", name, text);
    bsp_display_unlock();
}

void ui_show_confirm(uint32_t id, const char *tool, const char *summary, ui_risk_t risk, const char *project)
{
    bsp_display_lock(0);
    modal_show(id, tool, summary, risk, project);
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
