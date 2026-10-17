/*
 * Round 360x360 UI: usage dashboard plus the permission confirm modal.
 * Public functions take the LVGL lock themselves; call them from any task.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

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

/* Builds the dashboard. Requires bsp_display_start() to have run. */
void ui_init(ui_decision_cb_t on_decision, ui_link_probe_t link_probe);

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
/* Bluetooth pairing started (show `passkey` for the user to type on the host) or ended. */
void ui_show_pairing(bool active, uint32_t passkey);
