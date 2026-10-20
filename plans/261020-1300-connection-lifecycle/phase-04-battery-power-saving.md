---
phase: 4
title: Battery power saving
status: completed
priority: P2
dependencies:
  - 2
effort: 1 d
---

# Phase 4: Battery power saving

## Overview
On battery with no bridge link for 10 min, turn the backlight off and slow down UI/face work and BLE advertising. Touch, motion or a restored link wakes it.

## Requirements
- Condition: fuel gauge present AND not charging AND no USB/BLE link for `SCREEN_OFF_IDLE_MS = 10 min`.
- Action: backlight 0, pause face animation timers, longer advertising interval when Bluetooth is on.
- Wake: touch, IMU `moved/shaken/picked up`, link becoming alive, incoming agent event. Restore brightness from settings.
- Never when charging/USB-powered or when the gauge is unavailable.
- Non-functional: no deep sleep; first touch after wake must not trigger a UI action (swallow it).

## Architecture
- `ui.c` already tracks `s_last_activity`, link state and dozing; add a `s_screen_off` state checked in the same periodic update. Battery state from `battery.c` poll (expose last `charging`/`present`).
- Backlight via `bsp_display_brightness_set(0)` / restore `s_settings.brightness` (needs a callback or settings pointer; `ui_init` already gets `settings`).
- Advertising interval: `ble_link_set_low_power(bool)` restarting advertising with larger `itvl_min/max` (e.g. 1 s–2 s) vs default.
- Touch swallow: transparent top-layer catcher while screen is off, like the dashboard tap layer.

## Related Code Files
- Modify: `echoear-firmware/main/ui.c`, `ui.h`, `battery.{c,h}`, `ble_link.{c,h}`, `main.c` (body sensor events → wake)

## Implementation Steps
1. Expose battery presence/charging from `battery.c`.
2. Screen-off state machine in `ui.c` with wake sources.
3. Low-power advertising in `ble_link.c`.
4. Debug override: shorten timeout via a compile-time define for testing (not a runtime setting).
5. Build, flash, test on battery.

## Success Criteria
- [ ] On battery, unplugged, bridge stopped: screen off after 10 min (verify with short debug timeout first).
- [ ] Touch or pick-up wakes; first touch does not open Settings/press buttons.
- [ ] Bluetooth on: Mac reconnects after wake within a few seconds; link arrival wakes the screen.
- [ ] USB plugged or charging: never turns off.

## Risk Assessment
- `charging` false when full on USB power? BQ27220 current ~0 when full → treat "USB connected" (link or `usb_serial_jtag_is_connected()`) as powered too.
- Slower advertising lengthens reconnect; accepted for battery saving.
