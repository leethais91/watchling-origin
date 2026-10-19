/*
 * Round 360x360 UI: usage dashboard, settings screen and the permission confirm modal.
 * Public functions take the LVGL lock themselves; call them from any task.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "settings_screen.h"

/* Percentages 0..100, or -1 when unknown. cost_today < 0 when unknown. */
typedef struct {
    int claude_h5;
    int claude_d7;
    int codex_h5;
    int codex_d7;
    float cost_today;
} ui_usage_t;

typedef enum {
    UI_RISK_LOW,
    UI_RISK_MEDIUM,
    UI_RISK_HIGH,
} ui_risk_t;

typedef enum {
    UI_EVENT_DONE,
    UI_EVENT_ATTENTION,
    UI_EVENT_ERROR,
} ui_event_kind_t;

/* Called from the LVGL task when the user taps Approve or Deny. */
typedef void (*ui_decision_cb_t)(uint32_t id, bool approve);
/* Links the bridge is currently answering on (it keeps BLE up as a standby while on USB). */
typedef struct {
    bool usb;
    bool ble;
} ui_link_state_t;
/* Polled once per second; no live link hides any open modal. */
typedef ui_link_state_t (*ui_link_probe_t)(void);

/*
 * Builds the dashboard and the settings screen (showing `settings`). `on_settings` runs in the
 * LVGL task when the user changes one. Requires bsp_display_start() to have run.
 */
void ui_init(ui_decision_cb_t on_decision, ui_link_probe_t link_probe, const settings_values_t *settings,
             settings_change_cb_t on_settings);

void ui_set_usage(const ui_usage_t *usage);
/* The system clock now holds UTC; show it shifted by the host's offset (minutes east of UTC). */
void ui_set_clock_offset(int utc_offset_min);
/* Battery state from the fuel gauge; percent < 0 hides the indicator. */
void ui_set_battery(int percent, bool charging);
/* Shows the event in the status line and on the robot's face; also ends that agent's busy state. */
void ui_show_event(const char *source, ui_event_kind_t kind, const char *text);
/* An agent ("claude" / "codex") started or stopped working; the robot scans while any is busy. */
void ui_set_activity(const char *source, bool busy);
/* `source` is the asking agent ("claude" / "codex"), shown as the modal title. */
void ui_show_confirm(uint32_t id, const char *source, const char *tool, const char *summary, ui_risk_t risk);
/* Hides the modal if it still shows request `id`. */
void ui_cancel_confirm(uint32_t id);
/*
 * A speech clip started playing: show `mood` ("happy" / "surprised" / "sleep" / "idle") looking
 * `look` ("center" / "left" / "right", NULL = center) for `hold_ms`, then the base mood. Unknown
 * moods are ignored.
 */
void ui_show_speech_face(const char *mood, const char *look, uint32_t hold_ms);
/* Bluetooth pairing started (show `passkey` for the user to type on the host) or ended. */
void ui_show_pairing(bool active, uint32_t passkey);

/* Physical interaction picked up by the accelerometer. */
typedef enum {
    UI_BODY_MOVED,        /* picked up or bumped: startled */
    UI_BODY_TILT_LEFT,    /* leaning: the face slides and looks downhill until level again */
    UI_BODY_TILT_RIGHT,
    UI_BODY_LEVEL,        /* upright again after a tilt; rocking left and right makes it happy */
    UI_BODY_SHAKEN,       /* shaken hard: dizzy, eyes spinning */
    UI_BODY_ON_BACK,      /* lying on its back: gazes up at the stars */
    UI_BODY_OFF_BACK,
} ui_body_event_t;
void ui_body_event(ui_body_event_t event);

/* Quiet mode (lying on its face: no chimes, no speech) turned on or off: says so in the status line. */
void ui_set_quiet(bool quiet);

/*
 * Called in the LVGL task whenever the robot starts or stops needing the user: a permission
 * request on screen, or an agent waiting for input that nobody has touched the robot since.
 */
typedef void (*ui_needs_user_cb_t)(bool needs_user);
void ui_set_needs_user_cb(ui_needs_user_cb_t cb);

/*
 * Called in the LVGL task a few times a second: the gravity vector in mg, `lateral_mg` toward
 * the robot's right, `down_mg` toward the bottom of the screen. False = no reading.
 */
typedef bool (*ui_gravity_probe_t)(int *lateral_mg, int *down_mg);
void ui_set_gravity_probe(ui_gravity_probe_t probe);
