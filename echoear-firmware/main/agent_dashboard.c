#include "agent_dashboard.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include "assets/dashboard_assets.h"
#include "robot_face.h"

/*
 * Layout for the round panel (positions are offsets from the screen center). The usage
 * arcs hug the edge on the left (Claude) and right (Codex); everything else stays in a
 * ~150 px radius so nothing lands in the square corners or under the bezel.
 */

#define COLOR_BG             0x0B0F19
#define COLOR_ARC_TRACK      0x182638
#define COLOR_CLAUDE         0xFF854D
#define COLOR_CLAUDE_HI      0xFF9C55
#define COLOR_CODEX          0x10DFA8
#define COLOR_CODEX_HI       0x29FFD0
/* The 2 px line along each gauge's value: a near-white tint of the agent colour. */
#define COLOR_CLAUDE_GLINT   0xFFD3BD
#define COLOR_CODEX_GLINT    0xB8FFEB
#define COLOR_ELECTRIC_BLUE  0x00BFFF
#define COLOR_RIM_OUTER      0x1760A0
#define COLOR_RIM_INNER      0x103354
#define COLOR_ACCENT_BASE    0x008DCE
#define COLOR_TEXT_PRIMARY   0xF4F7FC
#define COLOR_TEXT_SOFT      0xCBD5E1
#define COLOR_TEXT_STATUS    0xD7E2EE
#define COLOR_TEXT_SECONDARY 0x94A3B8
#define COLOR_TEXT_MUTED     0x64748B
#define COLOR_OK             0x22C55E
#define COLOR_WARN           0xF59E0B
#define COLOR_DANGER         0xEF4444

/*
 * Usage gauge per side, all layers centred on the panel. Radii from the centre, outermost first:
 *   outline     2 px   r 169..171   decorative, faint
 *   ticks              r 164..167   15 ticks, one per 1/14 of the span; 0/50/100 % are longer
 *   track      16 px   r 145..161   dark, the full span (arc diameter 322)
 *   value      14 px   inset 1 px in the track, rounded ends, a small bright dot at the tip
 *   glint       2 px   on the value's centre line
 * Strokes are drawn inward from the arc object's edge, so a layer of width w is centred on the
 * track by sizing it ARC_DIAMETER - (TRACK_WIDTH - w).
 */
#define ARC_DIAMETER         322
#define TRACK_WIDTH          16
#define VALUE_WIDTH          14
#define GLINT_WIDTH          2
#define GLINT_OPA            LV_OPA_80
#define TIP_DOT_PX           6
#define OUTLINE_DIAMETER     342
#define OUTLINE_WIDTH        2
#define OUTLINE_OPA          LV_OPA_30
#define TICK_COUNT           15
#define TICK_R_INNER         164
#define TICK_R_MAJOR_INNER   162
#define TICK_R_OUTER         167
#define TICK_WIDTH           2
#define TICK_OPA             LV_OPA_40
#define TICK_MAJOR_OPA       LV_OPA_80
/* Values run 0..1000 (per mille) so the 600 ms sweep moves in sub-degree steps. */
#define ARC_RANGE            1000
#define ARC_ANIM_MS          600
/* LVGL angles start at 3 o'clock and grow clockwise: 135..240 is the left edge. */
#define CLAUDE_ARC_START     135
#define CLAUDE_ARC_END       240
#define CODEX_ARC_START      300
#define CODEX_ARC_END        45
#define ARC_SPAN             105

/*
 * HUD border just inside the glass, static, no glow. Radii from the centre:
 *   outer rim   2 px  r 176..178  with a short cyan highlight at 12 o'clock
 *   inner rim   1 px  r 173..174  darker, for depth
 *   accents     2 px  r 169..171  short segments at 12 / 3 / 6 / 9 o'clock, on the same circle as
 *                                 the gauges' faint outline so the side ones (in the agents'
 *                                 colours) read as lit marks on it instead of a second ring
 * The gauges' own ticks stand in for border ticks: a second tick ring would crowd them.
 * In LVGL angles 12 o'clock is 270, 3 o'clock 0, 6 o'clock 90 and 9 o'clock 180.
 */
#define RIM_OUTER_DIAMETER   356
#define RIM_OUTER_WIDTH      2
#define RIM_INNER_DIAMETER   348
#define RIM_INNER_WIDTH      1
#define ACCENT_DIAMETER      OUTLINE_DIAMETER
#define ACCENT_WIDTH         2
#define HIGHLIGHT_DEG        10
#define ACCENT_LONG_DEG      10 /* top and bottom */
#define ACCENT_SHORT_DEG     6  /* left and right */

#define TIME_Y               -142
#define DATE_Y               -119
#define ICONS_Y              -99
#define ROBOT_Y              -10
#define SIDE_X               114
#define PERCENT_Y            -26
#define LOGO_Y               0
#define WEEKLY_Y             22
#define COST_Y               88
#define COST_CAPTION_Y       111
#define STATUS_Y             134
#define STATUS_W             184
#define ICON_GAP             12

/*
 * Gauge styles are shared lv_style_t, not local styles: every local style property is a small
 * heap allocation, and LVGL's small allocations land in scarce internal RAM.
 */
typedef struct {
    lv_style_t outline_style;
    lv_style_t value_style;
    lv_style_t glint_style;
    lv_style_t tip_style; /* the dot on the value's moving end, attached only while the value is known */
    lv_obj_t *value;
    lv_obj_t *glint;
    lv_obj_t *percent;
    lv_obj_t *weekly;
    int target;   /* percent the gauge is heading to, -1 = unknown */
    int32_t shown; /* per mille the arcs show right now (mid-animation included) */
} usage_side_t;

/* One tick: a short radial line, precomputed in panel coordinates. */
typedef struct {
    lv_point_precise_t a;
    lv_point_precise_t b;
    lv_color_t color;
    lv_opa_t opa;
} gauge_tick_t;

/* Everything but the rim and the robot, so face mode can hide it in one go. */
static lv_obj_t *s_info;
static usage_side_t s_claude;
static usage_side_t s_codex;
static lv_style_t s_track_style; /* the same dark track on both sides */
static gauge_tick_t s_ticks[2 * TICK_COUNT];
static lv_obj_t *s_time;
static lv_obj_t *s_date;
static lv_obj_t *s_wifi;
static lv_obj_t *s_ble;
static lv_obj_t *s_usb;
static lv_obj_t *s_battery_icon;
static lv_obj_t *s_battery_text;
static lv_obj_t *s_cost;
static lv_obj_t *s_status;

/* ---------- helpers ---------- */

static void label_set_text_if_changed(lv_obj_t *label, const char *text)
{
    if (strcmp(lv_label_get_text(label), text) != 0) {
        lv_label_set_text(label, text);
    }
}

static void text_color_set_if_changed(lv_obj_t *obj, uint32_t hex)
{
    lv_color_t color = lv_color_hex(hex);
    if (!lv_color_eq(lv_obj_get_style_text_color(obj, LV_PART_MAIN), color)) {
        lv_obj_set_style_text_color(obj, color, 0);
    }
}

static lv_obj_t *label_create(lv_obj_t *parent, const lv_font_t *font, uint32_t color,
                              int32_t x, int32_t y, const char *text)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(label, text);
    lv_obj_align(label, LV_ALIGN_CENTER, x, y);
    return label;
}

/* A rounded stroke. Parts without one draw nothing: arc_width defaults to 0. */
static void arc_style_init(lv_style_t *style, int32_t width, uint32_t color, lv_opa_t opa)
{
    lv_style_init(style);
    lv_style_set_arc_width(style, width);
    lv_style_set_arc_rounded(style, true);
    lv_style_set_arc_color(style, lv_color_hex(color));
    lv_style_set_arc_opa(style, opa);
}

/* An arc centred on the panel, stroked on `part` (MAIN = the full span, INDICATOR = the value). */
static lv_obj_t *arc_create(lv_obj_t *parent, int32_t diameter, uint16_t start, uint16_t end, bool reverse,
                            lv_style_t *style, lv_part_t part)
{
    lv_obj_t *arc = lv_arc_create(parent);
    lv_obj_remove_style_all(arc);
    lv_obj_set_size(arc, diameter, diameter);
    lv_obj_center(arc);
    lv_obj_set_clickable(arc, false);
    lv_arc_set_mode(arc, reverse ? LV_ARC_MODE_REVERSE : LV_ARC_MODE_NORMAL);
    lv_arc_set_range(arc, 0, ARC_RANGE);
    lv_arc_set_bg_angles(arc, start, end);
    lv_arc_set_value(arc, 0);
    lv_obj_add_style(arc, style, part);
    return arc;
}

/*
 * Ticks for one side, bottom (0 %) to top (100 %). Precomputed once with float trig because
 * the 7.5 deg steps fall between lv_trigo_sin's whole degrees.
 */
static void ticks_compute(gauge_tick_t *ticks, int32_t cx, int32_t cy, float start_deg, float step_deg,
                          uint32_t color)
{
    for (int i = 0; i < TICK_COUNT; i++) {
        bool major = i == 0 || i == TICK_COUNT / 2 || i == TICK_COUNT - 1;
        float rad = (start_deg + step_deg * (float)i) * (float)M_PI / 180.0f;
        float c = cosf(rad);
        float sn = sinf(rad);
        float r_in = major ? TICK_R_MAJOR_INNER : TICK_R_INNER;
        ticks[i] = (gauge_tick_t){
            .a = {cx + (int32_t)lroundf(c * r_in), cy + (int32_t)lroundf(sn * r_in)},
            .b = {cx + (int32_t)lroundf(c * TICK_R_OUTER), cy + (int32_t)lroundf(sn * TICK_R_OUTER)},
            .color = lv_color_hex(color),
            .opa = major ? TICK_MAJOR_OPA : TICK_OPA,
        };
    }
}

/* Draws only the ticks that touch the area being refreshed, so other redraws stay cheap. */
static void ticks_draw_cb(lv_event_t *e)
{
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_draw_line_dsc_t dsc;
    lv_draw_line_dsc_init(&dsc);
    dsc.width = TICK_WIDTH;
    dsc.round_start = 1;
    dsc.round_end = 1;
    for (size_t i = 0; i < sizeof(s_ticks) / sizeof(s_ticks[0]); i++) {
        const gauge_tick_t *t = &s_ticks[i];
        const lv_area_t *clip = &layer->_clip_area;
        if (LV_MAX(t->a.x, t->b.x) + TICK_WIDTH < clip->x1 || LV_MIN(t->a.x, t->b.x) - TICK_WIDTH > clip->x2 ||
            LV_MAX(t->a.y, t->b.y) + TICK_WIDTH < clip->y1 || LV_MIN(t->a.y, t->b.y) - TICK_WIDTH > clip->y2) {
            continue;
        }
        dsc.p1 = t->a;
        dsc.p2 = t->b;
        dsc.color = t->color;
        dsc.opa = t->opa;
        lv_draw_line(layer, &dsc);
    }
}

/* One object draws both sides' ticks; it never changes, so it never invalidates itself. */
static void ticks_create(lv_obj_t *parent)
{
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_set_size(obj, lv_pct(100), lv_pct(100));
    lv_obj_center(obj);
    lv_obj_set_clickable(obj, false);
    lv_obj_update_layout(obj);
    lv_area_t a;
    lv_obj_get_coords(obj, &a);
    int32_t cx = a.x1 + lv_area_get_width(&a) / 2;
    int32_t cy = a.y1 + lv_area_get_height(&a) / 2;
    float step = (float)ARC_SPAN / (TICK_COUNT - 1);
    /* Both fill bottom-up: Claude clockwise from 135 deg, Codex counter-clockwise from 45 deg. */
    ticks_compute(&s_ticks[0], cx, cy, CLAUDE_ARC_START, step, COLOR_CLAUDE);
    ticks_compute(&s_ticks[TICK_COUNT], cx, cy, CODEX_ARC_END, -step, COLOR_CODEX);
    lv_obj_add_event_cb(obj, ticks_draw_cb, LV_EVENT_DRAW_MAIN, NULL);
}

/* Each arc invalidates only the slice between its old and new angle. */
static void gauge_set(usage_side_t *side, int32_t per_mille)
{
    side->shown = per_mille;
    lv_arc_set_value(side->value, per_mille);
    lv_arc_set_value(side->glint, per_mille);
    char text[16];
    snprintf(text, sizeof(text), "%d%%", (int)((per_mille + 5) / 10));
    label_set_text_if_changed(side->percent, text);
}

static void gauge_anim_cb(void *var, int32_t value)
{
    gauge_set(var, value);
}

static void usage_side_create(usage_side_t *side, lv_obj_t *parent, uint16_t start, uint16_t end,
                              uint32_t color, uint32_t glint, int32_t label_x, const lv_image_dsc_t *logo)
{
    /* The right arc runs clockwise from the top, so REVERSE makes both fill bottom-up. */
    bool reverse = start > end;

    arc_style_init(&side->outline_style, OUTLINE_WIDTH, color, OUTLINE_OPA);
    arc_style_init(&side->value_style, VALUE_WIDTH, color, LV_OPA_COVER);
    arc_style_init(&side->glint_style, GLINT_WIDTH, glint, GLINT_OPA);
    /* The knob sits on the moving end; shrunk to a dot inside the stroke it marks the tip. */
    lv_style_init(&side->tip_style);
    lv_style_set_radius(&side->tip_style, LV_RADIUS_CIRCLE);
    lv_style_set_bg_color(&side->tip_style, lv_color_hex(glint));
    lv_style_set_bg_opa(&side->tip_style, LV_OPA_COVER);
    lv_style_set_pad_all(&side->tip_style, -(VALUE_WIDTH - TIP_DOT_PX) / 2);

    arc_create(parent, OUTLINE_DIAMETER, start, end, reverse, &side->outline_style, LV_PART_MAIN);
    arc_create(parent, ARC_DIAMETER, start, end, reverse, &s_track_style, LV_PART_MAIN);
    side->value = arc_create(parent, ARC_DIAMETER - (TRACK_WIDTH - VALUE_WIDTH), start, end, reverse,
                             &side->value_style, LV_PART_INDICATOR);
    side->glint = arc_create(parent, ARC_DIAMETER - (TRACK_WIDTH - GLINT_WIDTH), start, end, reverse,
                             &side->glint_style, LV_PART_INDICATOR);

    side->percent = label_create(parent, &font_dash_bold_20, color, label_x, PERCENT_Y, "--");
    /* The agent's logo, tinted in its arc colour, in place of its name. */
    lv_obj_t *icon = lv_image_create(parent);
    lv_image_set_src(icon, logo);
    lv_obj_align(icon, LV_ALIGN_CENTER, label_x, LOGO_Y);
    side->weekly = label_create(parent, &lv_font_montserrat_14, COLOR_TEXT_SECONDARY, label_x, WEEKLY_Y, "");
    side->target = -1;
    side->shown = 0;
}

/* A new value sweeps the gauge there over ARC_ANIM_MS; repeats of the same value do nothing. */
static void usage_side_update(usage_side_t *side, int percent, int weekly)
{
    int target = percent < 0 ? -1 : percent > 100 ? 100 : percent;
    if (target != side->target) {
        /* A style change redraws the whole arc object, so the tip dot only toggles on unknown <-> known. */
        if (target < 0 && side->target >= 0) {
            lv_obj_remove_style(side->value, &side->tip_style, LV_PART_KNOB);
        } else if (target >= 0 && side->target < 0) {
            lv_obj_add_style(side->value, &side->tip_style, LV_PART_KNOB);
        }
        side->target = target;
        lv_anim_delete(side, gauge_anim_cb);
        if (target < 0) {
            gauge_set(side, 0);
            label_set_text_if_changed(side->percent, "--");
        } else {
            lv_anim_t a;
            lv_anim_init(&a);
            lv_anim_set_var(&a, side);
            lv_anim_set_exec_cb(&a, gauge_anim_cb);
            lv_anim_set_values(&a, side->shown, target * (ARC_RANGE / 100));
            lv_anim_set_duration(&a, ARC_ANIM_MS);
            lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
            lv_anim_start(&a);
        }
    }

    char text[16];
    lv_obj_set_hidden(side->weekly, weekly < 0);
    if (weekly >= 0) {
        snprintf(text, sizeof(text), "7d %d%%", weekly);
        label_set_text_if_changed(side->weekly, text);
    }
}

static lv_obj_t *icon_create(lv_obj_t *row, const char *symbol)
{
    lv_obj_t *icon = lv_label_create(row);
    lv_obj_set_style_text_font(icon, &lv_font_montserrat_14, 0);
    lv_label_set_text(icon, symbol);
    return icon;
}

static void status_icons_create(lv_obj_t *parent)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, ICON_GAP, 0);
    lv_obj_align(row, LV_ALIGN_CENTER, 0, ICONS_Y);

    s_wifi = icon_create(row, LV_SYMBOL_WIFI);
    lv_obj_set_style_text_color(s_wifi, lv_color_hex(COLOR_ELECTRIC_BLUE), 0);
    s_ble = icon_create(row, LV_SYMBOL_BLUETOOTH);
    lv_obj_set_style_text_color(s_ble, lv_color_hex(COLOR_ELECTRIC_BLUE), 0);
    s_usb = icon_create(row, LV_SYMBOL_USB);
    s_battery_icon = icon_create(row, "");
    s_battery_text = icon_create(row, "");
    lv_obj_set_style_text_color(s_battery_text, lv_color_hex(COLOR_TEXT_PRIMARY), 0);
}

static void battery_update(int percent, bool charging)
{
    lv_obj_set_hidden(s_battery_icon, percent < 0);
    lv_obj_set_hidden(s_battery_text, percent < 0);
    if (percent < 0) {
        return;
    }
    const char *symbol = percent > 80   ? LV_SYMBOL_BATTERY_FULL
                         : percent > 55 ? LV_SYMBOL_BATTERY_3
                         : percent > 30 ? LV_SYMBOL_BATTERY_2
                         : percent > 10 ? LV_SYMBOL_BATTERY_1
                                        : LV_SYMBOL_BATTERY_EMPTY;
    char text[16];
    snprintf(text, sizeof(text), "%d%%", percent);
    char icon[16];
    snprintf(icon, sizeof(icon), "%s%s", charging ? LV_SYMBOL_CHARGE " " : "", symbol);
    label_set_text_if_changed(s_battery_icon, icon);
    text_color_set_if_changed(s_battery_icon, charging || percent > 20 ? COLOR_OK : COLOR_DANGER);
    label_set_text_if_changed(s_battery_text, text);
}

static void rim_create(lv_obj_t *parent, int32_t diameter, int32_t width, uint32_t color)
{
    lv_obj_t *rim = lv_obj_create(parent);
    lv_obj_remove_style_all(rim);
    lv_obj_set_size(rim, diameter, diameter);
    lv_obj_center(rim);
    lv_obj_set_style_radius(rim, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(rim, width, 0);
    lv_obj_set_style_border_color(rim, lv_color_hex(color), 0);
}

/* A static segment of `len_deg` centred on `center_deg` (LVGL angles), drawn on the border. */
static void border_segment_create(lv_obj_t *parent, int32_t diameter, int32_t center_deg, int32_t len_deg,
                                  lv_style_t *style)
{
    int32_t start = (center_deg - len_deg / 2 + 360) % 360;
    arc_create(parent, diameter, start, (start + len_deg) % 360, false, style, LV_PART_MAIN);
}

static void border_create(lv_obj_t *parent)
{
    /* Shared by every segment of that colour (see the gauge styles for why not local styles). */
    static lv_style_t cyan, base, claude, codex;
    arc_style_init(&cyan, ACCENT_WIDTH, COLOR_ELECTRIC_BLUE, LV_OPA_COVER);
    arc_style_init(&base, ACCENT_WIDTH, COLOR_ACCENT_BASE, LV_OPA_COVER);
    arc_style_init(&claude, ACCENT_WIDTH, COLOR_CLAUDE, LV_OPA_COVER);
    arc_style_init(&codex, ACCENT_WIDTH, COLOR_CODEX, LV_OPA_COVER);

    rim_create(parent, RIM_OUTER_DIAMETER, RIM_OUTER_WIDTH, COLOR_RIM_OUTER);
    border_segment_create(parent, RIM_OUTER_DIAMETER, 270, HIGHLIGHT_DEG, &cyan);
    rim_create(parent, RIM_INNER_DIAMETER, RIM_INNER_WIDTH, COLOR_RIM_INNER);

    border_segment_create(parent, ACCENT_DIAMETER, 270, ACCENT_LONG_DEG, &cyan);
    border_segment_create(parent, ACCENT_DIAMETER, 0, ACCENT_SHORT_DEG, &codex);
    border_segment_create(parent, ACCENT_DIAMETER, 90, ACCENT_LONG_DEG, &base);
    border_segment_create(parent, ACCENT_DIAMETER, 180, ACCENT_SHORT_DEG, &claude);
}

/* ---------- public API ---------- */

void agent_dashboard_mock_data(agent_dashboard_data_t *out)
{
    *out = (agent_dashboard_data_t){
        .claude_usage_percent = 68,
        .codex_usage_percent = 42,
        .claude_weekly_percent = -1,
        .codex_weekly_percent = -1,
        .daily_cost = 3.45f,
        .wifi_connected = true,
        .ble_connected = true,
        .host_linked = false,
        .battery_percent = 100,
        .time_text = "10:24",
        .date_text = "Mon, Dec 9",
        .status_text = "Waiting for next task...",
        .status_tone = AGENT_DASHBOARD_TONE_NEUTRAL,
    };
}

lv_obj_t *agent_dashboard_create(lv_obj_t *parent)
{
    lv_obj_t *root = lv_obj_create(parent);
    lv_obj_remove_style_all(root);
    lv_obj_set_size(root, lv_pct(100), lv_pct(100));
    lv_obj_center(root);
    lv_obj_set_scrollable(root, false);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(root, lv_color_hex(COLOR_BG), 0);

    border_create(root);

    s_info = lv_obj_create(root);
    lv_obj_remove_style_all(s_info);
    lv_obj_set_size(s_info, lv_pct(100), lv_pct(100));
    lv_obj_center(s_info);
    lv_obj_set_scrollable(s_info, false);

    arc_style_init(&s_track_style, TRACK_WIDTH, COLOR_ARC_TRACK, LV_OPA_COVER);
    ticks_create(s_info);
    usage_side_create(&s_claude, s_info, CLAUDE_ARC_START, CLAUDE_ARC_END, COLOR_CLAUDE, COLOR_CLAUDE_GLINT,
                      -SIDE_X, &img_logo_claude);
    usage_side_create(&s_codex, s_info, CODEX_ARC_START, CODEX_ARC_END, COLOR_CODEX, COLOR_CODEX_GLINT,
                      SIDE_X, &img_logo_codex);

    s_time = label_create(s_info, &font_dash_bold_30, COLOR_TEXT_PRIMARY, 0, TIME_Y, "--:--");
    s_date = label_create(s_info, &lv_font_montserrat_16, COLOR_TEXT_SOFT, 0, DATE_Y, "");
    status_icons_create(s_info);

    s_cost = label_create(s_info, &font_dash_bold_30, COLOR_TEXT_PRIMARY, 0, COST_Y, "$--");
    label_create(s_info, &lv_font_montserrat_14, COLOR_TEXT_SOFT, 0, COST_CAPTION_Y, "Today");

    /* Agent text: monospace, and the only dashboard font with Vietnamese glyphs. */
    s_status = label_create(s_info, &font_mono_15, COLOR_TEXT_STATUS, 0, STATUS_Y, "");
    lv_obj_set_width(s_status, STATUS_W);
    lv_label_set_long_mode(s_status, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_height(s_status, lv_font_get_line_height(&font_mono_15));
    lv_obj_align(s_status, LV_ALIGN_CENTER, 0, STATUS_Y);

    /* Last, so the big face mode eyes draw above everything else. */
    robot_face_create(root, 0, ROBOT_Y);

    return root;
}

void agent_dashboard_update(const agent_dashboard_data_t *data)
{
    static const uint32_t tone_colors[] = {
        [AGENT_DASHBOARD_TONE_NEUTRAL] = COLOR_TEXT_STATUS,
        [AGENT_DASHBOARD_TONE_CLAUDE] = COLOR_CLAUDE_HI,
        [AGENT_DASHBOARD_TONE_CODEX] = COLOR_CODEX_HI,
        [AGENT_DASHBOARD_TONE_WARN] = COLOR_WARN,
        [AGENT_DASHBOARD_TONE_ERROR] = COLOR_DANGER,
    };

    usage_side_update(&s_claude, data->claude_usage_percent, data->claude_weekly_percent);
    usage_side_update(&s_codex, data->codex_usage_percent, data->codex_weekly_percent);

    label_set_text_if_changed(s_time, data->time_text[0] ? data->time_text : "--:--");
    label_set_text_if_changed(s_date, data->date_text);

    lv_obj_set_hidden(s_wifi, !data->wifi_connected);
    lv_obj_set_hidden(s_ble, !data->ble_connected);
    text_color_set_if_changed(s_usb, data->host_linked ? COLOR_ELECTRIC_BLUE : COLOR_TEXT_MUTED);
    battery_update(data->battery_percent, data->battery_charging);

    char cost[16] = "$--";
    if (data->daily_cost >= 0) {
        snprintf(cost, sizeof(cost), "$%.2f", (double)data->daily_cost);
    }
    label_set_text_if_changed(s_cost, cost);

    label_set_text_if_changed(s_status, data->status_text);
    text_color_set_if_changed(s_status, tone_colors[data->status_tone]);
}

void agent_dashboard_set_face_mode(bool on)
{
    lv_obj_set_hidden(s_info, on);
    robot_face_set_fullscreen(on);
}
