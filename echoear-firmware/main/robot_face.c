#include "robot_face.h"

#include <stdbool.h>
#include "assets/dashboard_assets.h"

/*
 * The head is one image (robot_head.png, visor left empty); the eyes are LVGL objects on
 * top of it. Pill eyes cover most moods; "happy" swaps them for ^ ^ arcs and "error" for
 * x x crosses. Everything moves with small objects so each animation frame only redraws
 * the eyes, apart from the slow head bob (a few px, a handful of redraws per second).
 */

/* Visor centre in robot_head.png pixels; eye positions are relative to the head image. */
#define VISOR_CX         82
#define VISOR_CY         78
#define EYE_DX           24 /* each eye sits this far left/right of the visor centre */
#define EYE_GLOW_W       14
#define EYE_CLOSED_H     2
#define SHAPE_PX         24 /* box of the happy arcs and error crosses */
#define STROKE_PX        5
#define HAPPY_ARC_DY     5 /* the arc is the top of a circle; drop it so it looks centred */

#define COLOR_EYE        0x46E6FF
#define COLOR_EYE_SLEEP  0x1C5470
#define COLOR_HAPPY      0x4ADE80
#define COLOR_ATTENTION  0xFBBF24
#define COLOR_ERROR      0xF87171

#define GAZE_PX          7
#define BLINK_CLOSE_MS   70
#define BLINK_OPEN_MS    110
#define BLINK_MIN_MS     2000
#define BLINK_MAX_MS     6000
#define DOUBLE_BLINK_PCT 20
#define GLANCE_MIN_MS    7000
#define GLANCE_MAX_MS    14000
#define GLANCE_MOVE_MS   220
#define GLANCE_HOLD_MS   1200
#define SWEEP_MS         750
#define PULSE_MS         520
#define BOB_PX           2
#define BOB_MS           1700
#define BOUNCE_PX        8
#define BOUNCE_UP_MS     160
#define BOUNCE_DOWN_MS   260
#define SHAKE_PX         4
#define SHAKE_MS         45
#define SHAKE_SWINGS     3

typedef struct {
    int32_t w; /* pill eye size; w == 0 means the mood draws arcs or crosses instead */
    int32_t h;
    uint32_t color;
    lv_opa_t glow_opa;
    bool blinks;
    bool bobs;
} mood_style_t;

static const mood_style_t MOODS[] = {
    [ROBOT_MOOD_IDLE] = {22, 30, COLOR_EYE, LV_OPA_60, true, true},
    [ROBOT_MOOD_BUSY] = {22, 18, COLOR_EYE, LV_OPA_60, true, true},
    [ROBOT_MOOD_SLEEP] = {22, 4, COLOR_EYE_SLEEP, LV_OPA_TRANSP, false, false},
    [ROBOT_MOOD_HAPPY] = {0, 0, COLOR_HAPPY, LV_OPA_TRANSP, false, true},
    [ROBOT_MOOD_ATTENTION] = {26, 34, COLOR_ATTENTION, LV_OPA_60, true, true},
    [ROBOT_MOOD_ERROR] = {0, 0, COLOR_ERROR, LV_OPA_TRANSP, false, true},
};

static const lv_point_precise_t CROSS_POINTS[2][2] = {
    {{0, 0}, {SHAPE_PX, SHAPE_PX}},
    {{SHAPE_PX, 0}, {0, SHAPE_PX}},
};

static lv_obj_t *s_rig;  /* bounces and shakes */
static lv_obj_t *s_head; /* bobs inside the rig; parent of the eyes */
static lv_obj_t *s_pill[2];
static lv_obj_t *s_arc[2];
static lv_obj_t *s_cross[2][2];
static lv_timer_t *s_react_timer;

static robot_mood_t s_base;
static robot_mood_t s_mood;
static int32_t s_gaze;   /* px, positive = looking right */
static int32_t s_closed; /* lid closure 0..100 while blinking */

/* ---------- eyes ---------- */

static void eyes_layout(void)
{
    const mood_style_t *m = &MOODS[s_mood];
    int32_t h = m->h - (m->h - EYE_CLOSED_H) * s_closed / 100;
    for (int i = 0; i < 2; i++) {
        int32_t cx = VISOR_CX + (i == 0 ? -EYE_DX : EYE_DX) + s_gaze;
        lv_obj_set_size(s_pill[i], m->w, h);
        lv_obj_set_pos(s_pill[i], cx - m->w / 2, VISOR_CY - h / 2);
        lv_obj_set_pos(s_arc[i], cx - SHAPE_PX / 2, VISOR_CY - SHAPE_PX / 2 + HAPPY_ARC_DY);
        for (int k = 0; k < 2; k++) {
            lv_obj_set_pos(s_cross[i][k], cx - SHAPE_PX / 2, VISOR_CY - SHAPE_PX / 2);
        }
    }
}

static void eyes_style(const mood_style_t *m, robot_mood_t mood)
{
    lv_color_t color = lv_color_hex(m->color);
    for (int i = 0; i < 2; i++) {
        lv_obj_set_hidden(s_pill[i], m->w == 0);
        lv_obj_set_style_bg_color(s_pill[i], color, 0);
        lv_obj_set_style_shadow_color(s_pill[i], color, 0);
        lv_obj_set_style_shadow_opa(s_pill[i], m->glow_opa, 0);

        lv_obj_set_hidden(s_arc[i], mood != ROBOT_MOOD_HAPPY);
        lv_obj_set_style_arc_color(s_arc[i], color, LV_PART_MAIN);
        for (int k = 0; k < 2; k++) {
            lv_obj_set_hidden(s_cross[i][k], mood != ROBOT_MOOD_ERROR);
            lv_obj_set_style_line_color(s_cross[i][k], color, 0);
        }
    }
}

/* ---------- animations ---------- */

static void closed_anim_cb(void *var, int32_t value)
{
    LV_UNUSED(var);
    s_closed = value;
    eyes_layout();
}

static void gaze_anim_cb(void *var, int32_t value)
{
    LV_UNUSED(var);
    s_gaze = value;
    eyes_layout();
}

static void glow_anim_cb(void *var, int32_t value)
{
    LV_UNUSED(var);
    for (int i = 0; i < 2; i++) {
        lv_obj_set_style_shadow_opa(s_pill[i], (lv_opa_t)value, 0);
    }
}

/* Translations are only touched when the pixel value changes, so slow motion stays cheap. */
static void translate_set(lv_obj_t *obj, lv_style_prop_t prop, int32_t value)
{
    if (lv_obj_get_style_prop(obj, LV_PART_MAIN, prop).num == value) {
        return;
    }
    if (prop == LV_STYLE_TRANSLATE_X) {
        lv_obj_set_style_translate_x(obj, value, 0);
    } else {
        lv_obj_set_style_translate_y(obj, value, 0);
    }
}

static void bob_anim_cb(void *var, int32_t value)
{
    LV_UNUSED(var);
    translate_set(s_head, LV_STYLE_TRANSLATE_Y, value);
}

static void bounce_anim_cb(void *var, int32_t value)
{
    LV_UNUSED(var);
    translate_set(s_rig, LV_STYLE_TRANSLATE_Y, value);
}

static void shake_anim_cb(void *var, int32_t value)
{
    LV_UNUSED(var);
    translate_set(s_rig, LV_STYLE_TRANSLATE_X, value);
}

static void shake_done_cb(lv_anim_t *a)
{
    LV_UNUSED(a);
    translate_set(s_rig, LV_STYLE_TRANSLATE_X, 0);
}

/* All animations hang off s_head, told apart by their exec callback. */
static void anim_prepare(lv_anim_t *a, lv_anim_exec_xcb_t cb, int32_t from, int32_t to, uint32_t ms)
{
    lv_anim_init(a);
    lv_anim_set_var(a, s_head);
    lv_anim_set_exec_cb(a, cb);
    lv_anim_set_values(a, from, to);
    lv_anim_set_duration(a, ms);
    lv_anim_set_path_cb(a, lv_anim_path_ease_in_out);
}

static void anim_ping_pong(lv_anim_exec_xcb_t cb, int32_t from, int32_t to, uint32_t ms)
{
    lv_anim_t a;
    anim_prepare(&a, cb, from, to, ms);
    lv_anim_set_reverse_duration(&a, ms);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&a);
}

static void blink_timer_cb(lv_timer_t *timer)
{
    lv_timer_set_period(timer, lv_rand(BLINK_MIN_MS, BLINK_MAX_MS));
    if (!MOODS[s_mood].blinks) {
        return;
    }
    lv_anim_t a;
    anim_prepare(&a, closed_anim_cb, 0, 100, BLINK_CLOSE_MS);
    lv_anim_set_reverse_duration(&a, BLINK_OPEN_MS);
    if (lv_rand(0, 99) < DOUBLE_BLINK_PCT) {
        lv_anim_set_repeat_count(&a, 2);
    }
    lv_anim_start(&a);
}

/* Idle only: look to one side for a moment, then back. */
static void glance_timer_cb(lv_timer_t *timer)
{
    lv_timer_set_period(timer, lv_rand(GLANCE_MIN_MS, GLANCE_MAX_MS));
    if (s_mood != ROBOT_MOOD_IDLE) {
        return;
    }
    lv_anim_t a;
    anim_prepare(&a, gaze_anim_cb, 0, lv_rand(0, 1) ? GAZE_PX : -GAZE_PX, GLANCE_MOVE_MS);
    lv_anim_set_reverse_delay(&a, GLANCE_HOLD_MS);
    lv_anim_set_reverse_duration(&a, GLANCE_MOVE_MS);
    lv_anim_start(&a);
}

/* ---------- moods ---------- */

static void mood_apply(robot_mood_t mood, robot_look_t look)
{
    const mood_style_t *m = &MOODS[mood];
    s_mood = mood;

    lv_anim_delete(s_head, closed_anim_cb);
    lv_anim_delete(s_head, gaze_anim_cb);
    lv_anim_delete(s_head, glow_anim_cb);
    lv_anim_delete(s_head, bounce_anim_cb);
    lv_anim_delete(s_head, shake_anim_cb);
    translate_set(s_rig, LV_STYLE_TRANSLATE_X, 0);
    translate_set(s_rig, LV_STYLE_TRANSLATE_Y, 0);
    s_closed = 0;
    s_gaze = look == ROBOT_LOOK_LEFT ? -GAZE_PX : look == ROBOT_LOOK_RIGHT ? GAZE_PX : 0;

    eyes_style(m, mood);
    eyes_layout();

    if (!m->bobs) {
        lv_anim_delete(s_head, bob_anim_cb);
        translate_set(s_head, LV_STYLE_TRANSLATE_Y, 0);
    } else if (lv_anim_get(s_head, bob_anim_cb) == NULL) {
        anim_ping_pong(bob_anim_cb, -BOB_PX, BOB_PX, BOB_MS);
    }

    lv_anim_t a;
    switch (mood) {
    case ROBOT_MOOD_BUSY:
        anim_ping_pong(gaze_anim_cb, -GAZE_PX, GAZE_PX, SWEEP_MS);
        break;
    case ROBOT_MOOD_ATTENTION:
        anim_ping_pong(glow_anim_cb, LV_OPA_20, LV_OPA_COVER, PULSE_MS);
        break;
    case ROBOT_MOOD_HAPPY:
        anim_prepare(&a, bounce_anim_cb, 0, -BOUNCE_PX, BOUNCE_UP_MS);
        lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
        lv_anim_set_reverse_duration(&a, BOUNCE_DOWN_MS);
        lv_anim_set_repeat_count(&a, 2);
        lv_anim_start(&a);
        break;
    case ROBOT_MOOD_ERROR:
        anim_prepare(&a, shake_anim_cb, -SHAKE_PX, SHAKE_PX, SHAKE_MS);
        lv_anim_set_path_cb(&a, lv_anim_path_linear);
        lv_anim_set_reverse_duration(&a, SHAKE_MS);
        lv_anim_set_repeat_count(&a, SHAKE_SWINGS);
        lv_anim_set_completed_cb(&a, shake_done_cb);
        lv_anim_start(&a);
        break;
    default:
        break;
    }
}

static void react_timer_cb(lv_timer_t *timer)
{
    lv_timer_pause(timer);
    mood_apply(s_base, ROBOT_LOOK_CENTER);
}

/* ---------- public API ---------- */

lv_obj_t *robot_face_create(lv_obj_t *parent, int32_t x, int32_t y)
{
    s_rig = lv_obj_create(parent);
    lv_obj_remove_style_all(s_rig);
    lv_obj_set_size(s_rig, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_scrollable(s_rig, false);
    lv_obj_set_overflow_visible(s_rig, true); /* the head bobs past the rig's edge */
    lv_obj_align(s_rig, LV_ALIGN_CENTER, x, y);

    s_head = lv_image_create(s_rig);
    lv_image_set_src(s_head, &img_robot_head);

    for (int i = 0; i < 2; i++) {
        lv_obj_t *pill = lv_obj_create(s_head);
        lv_obj_remove_style_all(pill);
        lv_obj_set_style_radius(pill, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(pill, LV_OPA_COVER, 0);
        lv_obj_set_style_shadow_width(pill, EYE_GLOW_W, 0);
        s_pill[i] = pill;

        lv_obj_t *arc = lv_arc_create(s_head);
        lv_obj_remove_style_all(arc);
        lv_obj_set_size(arc, SHAPE_PX, SHAPE_PX);
        lv_obj_set_clickable(arc, false);
        lv_arc_set_bg_angles(arc, 190, 350);
        lv_obj_set_style_arc_width(arc, STROKE_PX, LV_PART_MAIN);
        lv_obj_set_style_arc_rounded(arc, true, LV_PART_MAIN);
        lv_obj_set_style_arc_opa(arc, LV_OPA_TRANSP, LV_PART_INDICATOR);
        s_arc[i] = arc;

        for (int k = 0; k < 2; k++) {
            lv_obj_t *line = lv_line_create(s_head);
            lv_line_set_points(line, CROSS_POINTS[k], 2);
            lv_obj_set_style_line_width(line, STROKE_PX, 0);
            lv_obj_set_style_line_rounded(line, true, 0);
            s_cross[i][k] = line;
        }
    }

    lv_timer_create(blink_timer_cb, BLINK_MIN_MS, NULL);
    lv_timer_create(glance_timer_cb, GLANCE_MIN_MS, NULL);
    s_react_timer = lv_timer_create(react_timer_cb, 1000, NULL);
    lv_timer_pause(s_react_timer);

    s_base = ROBOT_MOOD_IDLE;
    mood_apply(ROBOT_MOOD_IDLE, ROBOT_LOOK_CENTER);
    return s_rig;
}

void robot_face_set_base(robot_mood_t mood)
{
    if (mood == s_base) {
        return;
    }
    s_base = mood;
    lv_timer_pause(s_react_timer);
    mood_apply(mood, ROBOT_LOOK_CENTER);
}

void robot_face_react(robot_mood_t mood, robot_look_t look, uint32_t hold_ms)
{
    lv_timer_pause(s_react_timer);
    mood_apply(mood, look);
    if (hold_ms > 0) {
        lv_timer_set_period(s_react_timer, hold_ms);
        lv_timer_reset(s_react_timer);
        lv_timer_resume(s_react_timer);
    }
}
