#include "agent_dashboard.h"

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
#define COLOR_CLAUDE         0xD97757
#define COLOR_CLAUDE_HI      0xFF9C55
#define COLOR_CODEX          0x10DFA8
#define COLOR_CODEX_HI       0x29FFD0
#define COLOR_ELECTRIC_BLUE  0x00BFFF
#define COLOR_GLOW_BLUE      0x167DFF
#define COLOR_TEXT_PRIMARY   0xF4F7FC
#define COLOR_TEXT_SOFT      0xCBD5E1
#define COLOR_TEXT_STATUS    0xD7E2EE
#define COLOR_TEXT_SECONDARY 0x94A3B8
#define COLOR_TEXT_MUTED     0x64748B
#define COLOR_OK             0x22C55E
#define COLOR_WARN           0xF59E0B
#define COLOR_DANGER         0xEF4444

/* Arc stack per side: soft glow behind, track + value, thin highlight centred on the value. */
#define ARC_DIAMETER         320
#define ARC_WIDTH            16
#define ARC_GLOW_WIDTH       24
#define ARC_GLOW_OPA         LV_OPA_40
#define ARC_HI_WIDTH         10
#define ARC_HI_OPA           LV_OPA_COVER
/* LVGL angles start at 3 o'clock and grow clockwise: 135..240 is the left edge. */
#define CLAUDE_ARC_START     135
#define CLAUDE_ARC_END       240
#define CODEX_ARC_START      300
#define CODEX_ARC_END        45

/* Neon rim: a crisp ring plus a wider faint one for the glow, just inside the glass. */
#define RIM_DIAMETER         354
#define RIM_WIDTH            2
#define RIM_OPA              LV_OPA_70
#define RIM_GLOW_DIAMETER    360
#define RIM_GLOW_WIDTH       8
#define RIM_GLOW_OPA         LV_OPA_20

#define TIME_Y               -142
#define DATE_Y               -119
#define ICONS_Y              -99
#define ROBOT_Y              -10
#define SIDE_X               114
#define PERCENT_Y            -22
#define PROVIDER_Y           -2
#define WEEKLY_Y             16
#define COST_Y               88
#define COST_CAPTION_Y       111
#define STATUS_Y             134
#define STATUS_W             184
#define ICON_GAP             12

typedef struct {
    lv_obj_t *glow;
    lv_obj_t *value;
    lv_obj_t *highlight;
    lv_obj_t *percent;
    lv_obj_t *weekly;
} usage_side_t;

static usage_side_t s_claude;
static usage_side_t s_codex;
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

/*
 * One arc layer. Strokes are drawn inward from the object edge, so layers of different
 * widths are centred on each other by growing/shrinking the object by the width delta.
 */
static lv_obj_t *arc_layer_create(lv_obj_t *parent, int32_t width, uint32_t track_color,
                                  lv_opa_t track_opa, uint32_t color, lv_opa_t opa, bool reverse)
{
    int32_t diameter = ARC_DIAMETER + (width - ARC_WIDTH);
    lv_obj_t *arc = lv_arc_create(parent);
    lv_obj_remove_style_all(arc);
    lv_obj_set_size(arc, diameter, diameter);
    lv_obj_center(arc);
    lv_obj_set_clickable(arc, false);
    lv_arc_set_mode(arc, reverse ? LV_ARC_MODE_REVERSE : LV_ARC_MODE_NORMAL);
    lv_arc_set_range(arc, 0, 100);
    lv_arc_set_value(arc, 0);

    lv_obj_set_style_arc_width(arc, width, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(arc, true, LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc, lv_color_hex(track_color), LV_PART_MAIN);
    lv_obj_set_style_arc_opa(arc, track_opa, LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, width, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(arc, true, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc, lv_color_hex(color), LV_PART_INDICATOR);
    lv_obj_set_style_arc_opa(arc, opa, LV_PART_INDICATOR);
    return arc;
}

static void usage_side_create(usage_side_t *side, lv_obj_t *parent, uint16_t start, uint16_t end,
                              uint32_t color, uint32_t highlight, int32_t label_x, const char *name)
{
    /* The right arc runs clockwise from the top, so REVERSE makes both fill bottom-up. */
    bool reverse = start > end;
    side->glow = arc_layer_create(parent, ARC_GLOW_WIDTH, 0, LV_OPA_TRANSP, color, ARC_GLOW_OPA, reverse);
    side->value = arc_layer_create(parent, ARC_WIDTH, COLOR_ARC_TRACK, LV_OPA_COVER, color, LV_OPA_COVER, reverse);
    side->highlight = arc_layer_create(parent, ARC_HI_WIDTH, 0, LV_OPA_TRANSP, highlight, ARC_HI_OPA, reverse);
    lv_obj_t *arcs[] = {side->glow, side->value, side->highlight};
    for (size_t i = 0; i < sizeof(arcs) / sizeof(arcs[0]); i++) {
        lv_arc_set_bg_angles(arcs[i], start, end);
    }

    side->percent = label_create(parent, &font_dash_bold_20, color, label_x, PERCENT_Y, "--");
    label_create(parent, &font_dash_semibold_14, color, label_x, PROVIDER_Y, name);
    side->weekly = label_create(parent, &lv_font_montserrat_14, COLOR_TEXT_SECONDARY, label_x, WEEKLY_Y, "");
}

static void usage_side_update(const usage_side_t *side, int percent, int weekly)
{
    int value = percent > 100 ? 100 : percent > 0 ? percent : 0;
    lv_arc_set_value(side->glow, value);
    lv_arc_set_value(side->value, value);
    lv_arc_set_value(side->highlight, value);

    char text[16] = "--";
    if (percent >= 0) {
        snprintf(text, sizeof(text), "%d%%", percent);
    }
    label_set_text_if_changed(side->percent, text);

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

static void rim_create(lv_obj_t *parent, int32_t diameter, int32_t width, lv_opa_t opa)
{
    lv_obj_t *rim = lv_obj_create(parent);
    lv_obj_remove_style_all(rim);
    lv_obj_set_size(rim, diameter, diameter);
    lv_obj_center(rim);
    lv_obj_set_style_radius(rim, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(rim, width, 0);
    lv_obj_set_style_border_color(rim, lv_color_hex(COLOR_GLOW_BLUE), 0);
    lv_obj_set_style_border_opa(rim, opa, 0);
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

    rim_create(root, RIM_GLOW_DIAMETER, RIM_GLOW_WIDTH, RIM_GLOW_OPA);
    rim_create(root, RIM_DIAMETER, RIM_WIDTH, RIM_OPA);

    usage_side_create(&s_claude, root, CLAUDE_ARC_START, CLAUDE_ARC_END, COLOR_CLAUDE, COLOR_CLAUDE_HI,
                      -SIDE_X, "Claude");
    usage_side_create(&s_codex, root, CODEX_ARC_START, CODEX_ARC_END, COLOR_CODEX, COLOR_CODEX_HI,
                      SIDE_X, "Codex");

    s_time = label_create(root, &font_dash_bold_30, COLOR_TEXT_PRIMARY, 0, TIME_Y, "--:--");
    s_date = label_create(root, &lv_font_montserrat_16, COLOR_TEXT_SOFT, 0, DATE_Y, "");
    status_icons_create(root);

    robot_face_create(root, 0, ROBOT_Y);

    s_cost = label_create(root, &font_dash_bold_30, COLOR_TEXT_PRIMARY, 0, COST_Y, "$--");
    label_create(root, &lv_font_montserrat_14, COLOR_TEXT_SOFT, 0, COST_CAPTION_Y, "Today");

    /* Agent text: monospace, and the only dashboard font with Vietnamese glyphs. */
    s_status = label_create(root, &font_mono_15, COLOR_TEXT_STATUS, 0, STATUS_Y, "");
    lv_obj_set_width(s_status, STATUS_W);
    lv_label_set_long_mode(s_status, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_height(s_status, lv_font_get_line_height(&font_mono_15));
    lv_obj_align(s_status, LV_ALIGN_CENTER, 0, STATUS_Y);

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
