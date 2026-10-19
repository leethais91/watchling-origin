#include "robot_face.h"

#include <stdbool.h>
#include "assets/dashboard_assets.h"

/*
 * The head is one image (robot_head.png, visor left empty); the eyes are LVGL objects on
 * top of it. Pill eyes cover most moods; "happy" swaps them for ^ ^ arcs, "sleep" for
 * closed-lid arcs and "error" for > < squints. Everything moves with small objects so each animation frame only redraws
 * the eyes, apart from the slow head bob (a few px, a handful of redraws per second).
 *
 * Fullscreen hides the helmet and multiplies every eye size, offset and motion by
 * FULL_SCALE instead of zooming a layer, so the big eyes stay crisp and cheap to redraw.
 */

/* Visor centre in robot_head.png pixels; eye positions are relative to the head image. */
#define VISOR_CX         82
#define VISOR_CY         78
#define FULL_SCALE       3
#define FULL_HEAD_PX     360 /* the head box in fullscreen: the whole panel */
#define FULL_GLOW_W      24  /* a 3x glow costs a lot of shadow rendering; less reads as well */
#define EYE_DX           24 /* each eye sits this far left/right of the visor centre */
#define EYE_GLOW_W       14
#define EYE_CLOSED_H     2
#define SHAPE_PX         24 /* box of the happy and sleep arcs */
#define STROKE_PX        5
#define HAPPY_ARC_DY     5  /* the arc is the top of a circle; drop it so it looks centred */
#define SLEEP_ARC_DY     -8 /* closed lids are the bottom of a circle; lift them */
#define SQUINT_W         18 /* box of each > < squint */
#define SQUINT_H         22

#define COLOR_EYE        0x46E6FF
#define COLOR_EYE_SLEEP  0x5EC8E8
#define COLOR_ZZZ        0xA5E8FF
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
#define SURPRISE_HOP_PX  16 /* surprise hops twice as high as a happy bounce */
#define LEAN_PX          10 /* lean: the face slides this far downhill */
#define LEAN_GAZE_PX     9  /* and the eyes look further than a glance */
#define LEAN_MS          280
/* Dizzy: each eye circles its spot, the two half a turn apart, while the head wobbles. */
#define SPIN_PX          6
#define SPIN_MS          520
#define WOBBLE_PX        3
#define WOBBLE_MS        260
/*
 * Gravity roll: the eyes chase a target set by the tilt on a damped spring (Q8 small-head px,
 * stepped every ROLL_TICK_MS), so they overshoot a little and settle like marbles. A dead zone
 * keeps a desk that is not quite level from nudging them. Whatever the mood and the roll add
 * up to stays within EYE_ROAM, inside the visor.
 */
#define ROLL_TICK_MS     33
#define ROLL_DEAD_MG     60
#define ROLL_X_PX        14 /* at 1 g sideways */
#define ROLL_Y_PX        8  /* at 1 g upside down; standing or lying flat the eyes stay level */
#define ROLL_SPRING      40 /* /256 per tick */
#define ROLL_DAMPING     70 /* /256 per tick */
#define ROLL_SETTLE_Q8   16
#define EYE_ROAM_X       11
#define EYE_ROAM_Y       7

/* Stargaze: eyes raised, a few stars around them twinkling out of step. */
#define STARGAZE_DY      -6
#define STAR_COUNT       4
#define STAR_MS          900
#define SHAKE_PX         4
#define SHAKE_MS         45
#define SHAKE_SWINGS     3
/* Sleep: the lids glow slowly as if breathing, and three z's float up from the right eye. */
#define BREATH_MS        2200
#define ZZZ_COUNT        3
#define ZZZ_DX           6  /* first z, right of the right eye's centre */
#define ZZZ_DY           14 /* first z, above the visor centre */
#define ZZZ_STEP_X       5  /* each next z sits up and to the right */
#define ZZZ_STEP_Y       8
#define ZZZ_RISE_PX      6
#define ZZZ_DRIFT_PX     2
#define ZZZ_MS           2400

/* Every geometry constant above is in small-head pixels; S() scales it to the current mode. */
#define S(px)            ((px) * s_scale)

typedef struct {
    int32_t w; /* pill eye size; w == 0 means the mood draws arcs or squints instead */
    int32_t h;
    uint32_t color;
    lv_opa_t glow_opa;
    bool blinks;
    bool bobs;
} mood_style_t;

static const mood_style_t MOODS[] = {
    [ROBOT_MOOD_IDLE] = {22, 30, COLOR_EYE, LV_OPA_60, true, true},
    [ROBOT_MOOD_BUSY] = {22, 18, COLOR_EYE, LV_OPA_60, true, true},
    [ROBOT_MOOD_SLEEP] = {0, 0, COLOR_EYE_SLEEP, LV_OPA_TRANSP, false, false},
    [ROBOT_MOOD_HAPPY] = {0, 0, COLOR_HAPPY, LV_OPA_TRANSP, false, true},
    [ROBOT_MOOD_ATTENTION] = {26, 34, COLOR_ATTENTION, LV_OPA_60, true, true},
    [ROBOT_MOOD_ERROR] = {0, 0, COLOR_ERROR, LV_OPA_TRANSP, false, true},
    [ROBOT_MOOD_SURPRISED] = {34, 34, COLOR_EYE, LV_OPA_COVER, false, true},
    [ROBOT_MOOD_LEAN] = {22, 26, COLOR_EYE, LV_OPA_60, true, true},
    [ROBOT_MOOD_DIZZY] = {20, 20, COLOR_EYE, LV_OPA_60, false, false},
    [ROBOT_MOOD_STARGAZE] = {22, 30, COLOR_EYE, LV_OPA_COVER, true, true},
};

/* Star centres in small-head px from the visor centre, their size, and how long each twinkle takes. */
static const struct {
    int8_t dx;
    int8_t dy;
    uint8_t px;
    uint16_t ms;
} STARS[STAR_COUNT] = {
    {-44, -14, 9, STAR_MS},
    {-4, -24, 6, STAR_MS * 13 / 10},
    {40, -18, 10, STAR_MS * 8 / 10},
    {46, 12, 5, STAR_MS * 11 / 10},
};
#define STAR_STROKE_PX   2

static lv_obj_t *s_rig;  /* bounces, shakes and leans */
static lv_obj_t *s_head; /* bobs inside the rig; parent of the eyes */
static lv_obj_t *s_pill[2];
static lv_obj_t *s_arc[2];
static lv_obj_t *s_squint[2];
static lv_obj_t *s_zzz[ZZZ_COUNT];
static lv_obj_t *s_star[STAR_COUNT]; /* each a "+" sparkle: two crossed bars */
static lv_timer_t *s_react_timer;
/* lv_line keeps a pointer to its points, so they live here and are rescaled in place. */
static lv_point_precise_t s_squint_points[2][3];

static robot_mood_t s_base;
static robot_mood_t s_mood;
static robot_look_t s_look;
/* Set by robot_face_react(), cleared once the base mood is back. */
static bool s_reacting;
static int32_t s_gaze;   /* px, positive = looking right */
static int32_t s_gaze_y; /* px, negative = looking up */
static int32_t s_spin;   /* dizzy orbit angle in degrees */
/* Gravity roll, Q8 small-head px: where the eyes are heading, where they are, how fast. */
static int32_t s_roll_target[2];
static int32_t s_roll[2];
static int32_t s_roll_vel[2];
static int32_t s_closed; /* lid closure 0..100 while blinking */
static int32_t s_scale = 1;
static int32_t s_visor_x = VISOR_CX; /* visor centre inside s_head */
static int32_t s_visor_y = VISOR_CY;
static int32_t s_home_x; /* where robot_face_create() put the rig */
static int32_t s_home_y;

/* ---------- eyes ---------- */

static int32_t clamp_px(int32_t value, int32_t limit)
{
    return value < -limit ? -limit : value > limit ? limit : value;
}

static void eyes_layout(void)
{
    const mood_style_t *m = &MOODS[s_mood];
    int32_t w = S(m->w);
    int32_t h = S(m->h) - (S(m->h) - EYE_CLOSED_H) * s_closed / 100;
    int32_t shape = S(SHAPE_PX);
    int32_t look_x = clamp_px(s_gaze + (S(s_roll[0]) >> 8), S(EYE_ROAM_X));
    int32_t look_y = clamp_px(s_gaze_y + (S(s_roll[1]) >> 8), S(EYE_ROAM_Y));
    for (int i = 0; i < 2; i++) {
        int32_t cx = s_visor_x + (i == 0 ? -S(EYE_DX) : S(EYE_DX)) + look_x;
        int32_t cy = s_visor_y + look_y;
        if (s_mood == ROBOT_MOOD_DIZZY) {
            int32_t angle = (s_spin + i * 180) % 360;
            cx += S(SPIN_PX) * lv_trigo_cos(angle) >> LV_TRIGO_SHIFT;
            cy += S(SPIN_PX) * lv_trigo_sin(angle) >> LV_TRIGO_SHIFT;
        }
        lv_obj_set_size(s_pill[i], w, h);
        lv_obj_set_pos(s_pill[i], cx - w / 2, cy - h / 2);
        int32_t arc_dy = s_mood == ROBOT_MOOD_SLEEP ? SLEEP_ARC_DY : HAPPY_ARC_DY;
        lv_obj_set_pos(s_arc[i], cx - shape / 2, s_visor_y - shape / 2 + S(arc_dy));
        lv_obj_set_pos(s_squint[i], cx - S(SQUINT_W) / 2, s_visor_y - S(SQUINT_H) / 2);
    }
    for (int i = 0; i < ZZZ_COUNT; i++) {
        lv_obj_set_pos(s_zzz[i], s_visor_x + S(EYE_DX + ZZZ_DX + ZZZ_STEP_X * i),
                       s_visor_y - S(ZZZ_DY + ZZZ_STEP_Y * i));
    }
    for (int i = 0; i < STAR_COUNT; i++) {
        lv_obj_set_pos(s_star[i], s_visor_x + S(STARS[i].dx - STARS[i].px / 2), s_visor_y + S(STARS[i].dy - STARS[i].px / 2));
    }
}

/* Sizes that only change with the scale: shapes, strokes, glow and the z fonts. */
static void eyes_scale_apply(void)
{
    /* z, z, Z: each one a size up from the last. */
    static const lv_font_t *const ZZZ_FONTS_SMALL[ZZZ_COUNT] = {
        &lv_font_montserrat_14, &lv_font_montserrat_14, &lv_font_montserrat_16};
    static const lv_font_t *const ZZZ_FONTS_FULL[ZZZ_COUNT] = {
        &lv_font_montserrat_16, &lv_font_montserrat_20, &lv_font_montserrat_28};
    int32_t shape = S(SHAPE_PX);
    int32_t w = S(SQUINT_W);
    int32_t h = S(SQUINT_H);
    /* Left eye ">" and right eye "<", both pointing at the nose. */
    s_squint_points[0][0] = (lv_point_precise_t){0, 0};
    s_squint_points[0][1] = (lv_point_precise_t){w, h / 2};
    s_squint_points[0][2] = (lv_point_precise_t){0, h};
    s_squint_points[1][0] = (lv_point_precise_t){w, 0};
    s_squint_points[1][1] = (lv_point_precise_t){0, h / 2};
    s_squint_points[1][2] = (lv_point_precise_t){w, h};
    for (int i = 0; i < 2; i++) {
        lv_obj_set_style_shadow_width(s_pill[i], s_scale > 1 ? FULL_GLOW_W : EYE_GLOW_W, 0);
        lv_obj_set_size(s_arc[i], shape, shape);
        lv_obj_set_style_arc_width(s_arc[i], S(STROKE_PX), LV_PART_MAIN);
        lv_line_set_points(s_squint[i], s_squint_points[i], 3);
        lv_obj_set_style_line_width(s_squint[i], S(STROKE_PX), 0);
    }
    for (int i = 0; i < ZZZ_COUNT; i++) {
        lv_obj_set_style_text_font(s_zzz[i], s_scale > 1 ? ZZZ_FONTS_FULL[i] : ZZZ_FONTS_SMALL[i], 0);
    }
    for (int i = 0; i < STAR_COUNT; i++) {
        int32_t px = S(STARS[i].px);
        int32_t stroke = S(STAR_STROKE_PX);
        lv_obj_set_size(s_star[i], px, px);
        lv_obj_t *bar_h = lv_obj_get_child(s_star[i], 0);
        lv_obj_t *bar_v = lv_obj_get_child(s_star[i], 1);
        lv_obj_set_size(bar_h, px, stroke);
        lv_obj_set_size(bar_v, stroke, px);
        lv_obj_center(bar_h);
        lv_obj_center(bar_v);
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

        /* ^ for happy (top of the circle), closed lids for sleep (bottom). */
        lv_obj_set_hidden(s_arc[i], mood != ROBOT_MOOD_HAPPY && mood != ROBOT_MOOD_SLEEP);
        if (mood == ROBOT_MOOD_SLEEP) {
            lv_arc_set_bg_angles(s_arc[i], 20, 160);
        } else {
            lv_arc_set_bg_angles(s_arc[i], 190, 350);
        }
        lv_obj_set_style_arc_color(s_arc[i], color, LV_PART_MAIN);
        lv_obj_set_style_arc_opa(s_arc[i], LV_OPA_COVER, LV_PART_MAIN);

        lv_obj_set_hidden(s_squint[i], mood != ROBOT_MOOD_ERROR);
        lv_obj_set_style_line_color(s_squint[i], color, 0);
    }
    for (int i = 0; i < ZZZ_COUNT; i++) {
        lv_obj_set_hidden(s_zzz[i], mood != ROBOT_MOOD_SLEEP);
        lv_obj_set_style_text_opa(s_zzz[i], LV_OPA_TRANSP, 0);
    }
    for (int i = 0; i < STAR_COUNT; i++) {
        lv_obj_set_hidden(s_star[i], mood != ROBOT_MOOD_STARGAZE);
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

static void spin_anim_cb(void *var, int32_t value)
{
    LV_UNUSED(var);
    s_spin = value;
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

static void breath_anim_cb(void *var, int32_t value)
{
    LV_UNUSED(var);
    for (int i = 0; i < 2; i++) {
        lv_obj_set_style_arc_opa(s_arc[i], (lv_opa_t)value, LV_PART_MAIN);
    }
}

/* 0..100 for one z (the anim's var): fades in over the first quarter, out over the rest, drifting up. */
static void zzz_anim_cb(void *var, int32_t value)
{
    lv_obj_t *z = var;
    translate_set(z, LV_STYLE_TRANSLATE_Y, -S(ZZZ_RISE_PX) * value / 100);
    translate_set(z, LV_STYLE_TRANSLATE_X, S(ZZZ_DRIFT_PX) * value / 100);
    int32_t opa = value < 25 ? value * 4 : (100 - value) * 4 / 3;
    lv_obj_set_style_text_opa(z, (lv_opa_t)(LV_OPA_COVER * opa / 100), 0);
}

/* One star (the anim's var) fading in and out; the bars fade, not the star, so no layer is needed. */
static void star_anim_cb(void *var, int32_t value)
{
    for (int i = 0; i < 2; i++) {
        lv_obj_set_style_bg_opa(lv_obj_get_child(var, i), (lv_opa_t)value, 0);
    }
}

static void stars_start(void)
{
    for (int i = 0; i < STAR_COUNT; i++) {
        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, s_star[i]);
        lv_anim_set_exec_cb(&a, star_anim_cb);
        lv_anim_set_values(&a, LV_OPA_10, LV_OPA_COVER);
        lv_anim_set_duration(&a, STARS[i].ms);
        lv_anim_set_reverse_duration(&a, STARS[i].ms);
        lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
        lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
        lv_anim_start(&a);
    }
}

static void stars_stop(void)
{
    for (int i = 0; i < STAR_COUNT; i++) {
        lv_anim_delete(s_star[i], star_anim_cb);
    }
}

/* The z's are the exception to "everything hangs off s_head": each animates itself. */
static void zzz_start(void)
{
    for (int i = 0; i < ZZZ_COUNT; i++) {
        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, s_zzz[i]);
        lv_anim_set_exec_cb(&a, zzz_anim_cb);
        lv_anim_set_values(&a, 0, 100);
        lv_anim_set_duration(&a, ZZZ_MS);
        lv_anim_set_delay(&a, ZZZ_MS / ZZZ_COUNT * i);
        lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
        lv_anim_start(&a);
    }
}

static void zzz_stop(void)
{
    for (int i = 0; i < ZZZ_COUNT; i++) {
        lv_anim_delete(s_zzz[i], zzz_anim_cb);
    }
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
    anim_prepare(&a, gaze_anim_cb, 0, lv_rand(0, 1) ? S(GAZE_PX) : -S(GAZE_PX), GLANCE_MOVE_MS);
    lv_anim_set_reverse_delay(&a, GLANCE_HOLD_MS);
    lv_anim_set_reverse_duration(&a, GLANCE_MOVE_MS);
    lv_anim_start(&a);
}

/* One spring step per axis; the eyes are only laid out again when a pixel actually changes. */
static void roll_timer_cb(lv_timer_t *timer)
{
    LV_UNUSED(timer);
    bool moved = false;
    for (int i = 0; i < 2; i++) {
        int32_t pull = s_roll_target[i] - s_roll[i];
        if (LV_ABS(pull) < ROLL_SETTLE_Q8 && LV_ABS(s_roll_vel[i]) < ROLL_SETTLE_Q8) {
            s_roll_vel[i] = 0;
            continue;
        }
        int32_t before = S(s_roll[i]) >> 8;
        s_roll_vel[i] += (pull * ROLL_SPRING - s_roll_vel[i] * ROLL_DAMPING) / 256;
        s_roll[i] += s_roll_vel[i];
        moved |= (S(s_roll[i]) >> 8) != before;
    }
    if (moved) {
        eyes_layout();
    }
}

/* mg past the dead zone, scaled so 1 g gives `px_at_1g` (Q8). */
static int32_t roll_axis(int32_t mg, int32_t px_at_1g)
{
    int32_t past = LV_ABS(mg) - ROLL_DEAD_MG;
    if (past <= 0) {
        return 0;
    }
    int32_t q8 = past * px_at_1g * 256 / (1000 - ROLL_DEAD_MG);
    return mg < 0 ? -q8 : q8;
}

/* ---------- moods ---------- */

static void mood_apply(robot_mood_t mood, robot_look_t look)
{
    const mood_style_t *m = &MOODS[mood];
    s_mood = mood;
    s_look = look;

    lv_anim_delete(s_head, closed_anim_cb);
    lv_anim_delete(s_head, gaze_anim_cb);
    lv_anim_delete(s_head, glow_anim_cb);
    lv_anim_delete(s_head, bounce_anim_cb);
    lv_anim_delete(s_head, shake_anim_cb);
    lv_anim_delete(s_head, breath_anim_cb);
    lv_anim_delete(s_head, spin_anim_cb);
    zzz_stop();
    stars_stop();
    translate_set(s_rig, LV_STYLE_TRANSLATE_X, 0);
    translate_set(s_rig, LV_STYLE_TRANSLATE_Y, 0);
    s_closed = 0;
    s_spin = 0;
    s_gaze_y = mood == ROBOT_MOOD_STARGAZE ? S(STARGAZE_DY) : 0;
    s_gaze = mood == ROBOT_MOOD_LEAN       ? 0
             : look == ROBOT_LOOK_LEFT  ? -S(GAZE_PX)
             : look == ROBOT_LOOK_RIGHT ? S(GAZE_PX)
                                        : 0;

    eyes_style(m, mood);
    eyes_layout();

    if (!m->bobs) {
        lv_anim_delete(s_head, bob_anim_cb);
        translate_set(s_head, LV_STYLE_TRANSLATE_Y, 0);
    } else if (lv_anim_get(s_head, bob_anim_cb) == NULL) {
        anim_ping_pong(bob_anim_cb, -S(BOB_PX), S(BOB_PX), BOB_MS);
    }

    lv_anim_t a;
    switch (mood) {
    case ROBOT_MOOD_SLEEP:
        anim_ping_pong(breath_anim_cb, LV_OPA_40, LV_OPA_COVER, BREATH_MS);
        zzz_start();
        break;
    case ROBOT_MOOD_BUSY:
        anim_ping_pong(gaze_anim_cb, -S(GAZE_PX), S(GAZE_PX), SWEEP_MS);
        break;
    case ROBOT_MOOD_ATTENTION:
        anim_ping_pong(glow_anim_cb, LV_OPA_20, LV_OPA_COVER, PULSE_MS);
        break;
    case ROBOT_MOOD_HAPPY:
    case ROBOT_MOOD_SURPRISED:
        anim_prepare(&a, bounce_anim_cb, 0, -S(mood == ROBOT_MOOD_HAPPY ? BOUNCE_PX : SURPRISE_HOP_PX),
                     BOUNCE_UP_MS);
        lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
        lv_anim_set_reverse_duration(&a, BOUNCE_DOWN_MS);
        lv_anim_set_repeat_count(&a, mood == ROBOT_MOOD_HAPPY ? 2 : 1);
        lv_anim_start(&a);
        break;
    case ROBOT_MOOD_LEAN: {
        /* Slide in rather than jump: the gaze set above is only where the eyes end up. */
        int32_t dir = look == ROBOT_LOOK_LEFT ? -1 : 1;
        anim_prepare(&a, gaze_anim_cb, 0, dir * S(LEAN_GAZE_PX), LEAN_MS);
        lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
        lv_anim_start(&a);
        anim_prepare(&a, shake_anim_cb, 0, dir * S(LEAN_PX), LEAN_MS);
        lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
        lv_anim_start(&a);
        break;
    }
    case ROBOT_MOOD_DIZZY:
        anim_prepare(&a, spin_anim_cb, 0, 359, SPIN_MS);
        lv_anim_set_path_cb(&a, lv_anim_path_linear);
        lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
        lv_anim_start(&a);
        anim_ping_pong(shake_anim_cb, -S(WOBBLE_PX), S(WOBBLE_PX), WOBBLE_MS);
        break;
    case ROBOT_MOOD_STARGAZE:
        stars_start();
        break;
    case ROBOT_MOOD_ERROR:
        anim_prepare(&a, shake_anim_cb, -S(SHAKE_PX), S(SHAKE_PX), SHAKE_MS);
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
    s_reacting = false;
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
    s_home_x = x;
    s_home_y = y;

    s_head = lv_image_create(s_rig);
    lv_image_set_src(s_head, &img_robot_head);

    for (int i = 0; i < 2; i++) {
        lv_obj_t *pill = lv_obj_create(s_head);
        lv_obj_remove_style_all(pill);
        lv_obj_set_style_radius(pill, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(pill, LV_OPA_COVER, 0);
        s_pill[i] = pill;

        lv_obj_t *arc = lv_arc_create(s_head);
        lv_obj_remove_style_all(arc);
        lv_obj_set_clickable(arc, false);
        lv_arc_set_bg_angles(arc, 190, 350);
        lv_obj_set_style_arc_rounded(arc, true, LV_PART_MAIN);
        lv_obj_set_style_arc_opa(arc, LV_OPA_TRANSP, LV_PART_INDICATOR);
        s_arc[i] = arc;

        lv_obj_t *line = lv_line_create(s_head);
        lv_obj_set_style_line_rounded(line, true, 0);
        s_squint[i] = line;
    }
    for (int i = 0; i < ZZZ_COUNT; i++) {
        s_zzz[i] = lv_label_create(s_head);
        lv_label_set_text(s_zzz[i], i == ZZZ_COUNT - 1 ? "Z" : "z");
        lv_obj_set_style_text_color(s_zzz[i], lv_color_hex(COLOR_ZZZ), 0);
    }
    for (int i = 0; i < STAR_COUNT; i++) {
        lv_obj_t *star = lv_obj_create(s_head);
        lv_obj_remove_style_all(star);
        lv_obj_set_scrollable(star, false);
        for (int bar = 0; bar < 2; bar++) {
            lv_obj_t *b = lv_obj_create(star);
            lv_obj_remove_style_all(b);
            lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_bg_color(b, lv_color_hex(COLOR_ZZZ), 0);
        }
        s_star[i] = star;
    }
    eyes_scale_apply();

    lv_timer_create(blink_timer_cb, BLINK_MIN_MS, NULL);
    lv_timer_create(glance_timer_cb, GLANCE_MIN_MS, NULL);
    lv_timer_create(roll_timer_cb, ROLL_TICK_MS, NULL);
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
    s_reacting = false;
    lv_timer_pause(s_react_timer);
    mood_apply(mood, ROBOT_LOOK_CENTER);
}

void robot_face_react(robot_mood_t mood, robot_look_t look, uint32_t hold_ms)
{
    lv_timer_pause(s_react_timer);
    s_reacting = true;
    mood_apply(mood, look);
    if (hold_ms > 0) {
        lv_timer_set_period(s_react_timer, hold_ms);
        lv_timer_reset(s_react_timer);
        lv_timer_resume(s_react_timer);
    }
}

void robot_face_react_end(void)
{
    if (s_reacting) {
        react_timer_cb(s_react_timer);
    }
}

void robot_face_set_gravity(int32_t lateral_mg, int32_t down_mg)
{
    s_roll_target[0] = roll_axis(lateral_mg, ROLL_X_PX);
    /* Only upside down lifts the eyes; upright or flat on its back they stay level. */
    s_roll_target[1] = down_mg < 0 ? roll_axis(down_mg, ROLL_Y_PX) : 0;
}

bool robot_face_is_reacting(void)
{
    return s_reacting;
}

robot_mood_t robot_face_mood(void)
{
    return s_mood;
}

void robot_face_set_fullscreen(bool on)
{
    if ((s_scale > 1) == on) {
        return;
    }
    s_scale = on ? FULL_SCALE : 1;
    if (on) {
        lv_image_set_src(s_head, NULL);
        lv_obj_set_size(s_head, FULL_HEAD_PX, FULL_HEAD_PX);
        s_visor_x = FULL_HEAD_PX / 2;
        s_visor_y = FULL_HEAD_PX / 2;
    } else {
        lv_image_set_src(s_head, &img_robot_head);
        lv_obj_set_size(s_head, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        s_visor_x = VISOR_CX;
        s_visor_y = VISOR_CY;
    }
    lv_obj_align(s_rig, LV_ALIGN_CENTER, on ? 0 : s_home_x, on ? 0 : s_home_y);
    eyes_scale_apply();
    /* Restart the current mood so every running motion picks up the new amplitude. */
    lv_anim_delete(s_head, bob_anim_cb);
    translate_set(s_head, LV_STYLE_TRANSLATE_Y, 0);
    mood_apply(s_mood, s_look);
}
