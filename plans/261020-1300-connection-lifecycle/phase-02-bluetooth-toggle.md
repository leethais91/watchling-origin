---
phase: 2
title: Bluetooth toggle
status: completed
priority: P1
dependencies:
  - 1
effort: 1-1.5 d
---

# Phase 2: Bluetooth toggle

## Overview
Bluetooth switch in device Settings, stored in NVS, default **off**. Off stops advertising and drops any BLE connection; on resumes advertising so a bonded Mac reconnects. The device reports the setting to the bridge.

## Requirements
- Functional: switch in Settings next to Voice; persists across restarts; default off on fresh NVS and on upgrade (missing key → off).
- Off: terminate BLE connection, stop advertising, do not re-advertise on disconnect/adv-complete. Bonds kept.
- On: start advertising; bridge (BLE enabled) reconnects as standby even while USB is active.
- Protocol: device `hello` and `settings` carry `ble: bool`; bridge logs it and stores it for `bridge:status`.
- Non-functional: NimBLE stays initialized (no RAM change); pairing panel still works when on.

## Architecture
- `settings_values_t` gains `bool ble`; `settings_store` key `ble`, default `false`.
- `settings_screen.c`: new toggle under/next to Voice (`SETTINGS_FIELD_BLE`); layout validated in simulator (360 px round, 2 arcs + Voice already).
- `ble_link.c`: `static volatile bool s_enabled`; `ble_link_set_enabled(bool)`. `advertise()` returns early when disabled; disabling calls `ble_gap_adv_stop()` and `ble_gap_terminate(s_conn, BLE_ERR_REM_USER_CONN_TERM)` if connected. NimBLE calls must run on host-safe context: NimBLE GAP APIs are thread-safe from other tasks in ESP-IDF NimBLE; confirm, else post via `ble_npl_eventq`.
- `ble_link_start(..., bool enabled)` so boot honors the stored value (on_sync only advertises when enabled).
- `main.c`: `on_settings` handles `SETTINGS_FIELD_BLE` → `ble_link_set_enabled`, save, `send_settings()`; `send_hello()`/`send_voice()` JSON adds `"ble":true|false` (rename `send_voice` → `send_settings`).
- Bridge `messages.ts`: `hello`/`settings` get `ble?: boolean` (missing on older firmware → unknown). `bridge.ts` keeps `deviceBle: boolean | null`.

## Related Code Files
- Modify: `echoear-firmware/main/settings_screen.{c,h}`, `settings_store.c`, `ble_link.{c,h}`, `main.c`, `ui.c` (BT icon when disabled: hidden)
- Modify: `echoear-firmware/simulator/sim_main.c` (screenshot of new Settings)
- Modify: `pc-bridge/src/protocol/messages.ts`, `pc-bridge/src/bridge.ts`
- Modify: `docs/device-protocol.md` (fields), done again in Phase 5 for prose

## Implementation Steps
1. Settings model + NVS key with default off; load/save.
2. Settings screen toggle; simulator capture; adjust layout until readable.
3. `ble_link_set_enabled` + guarded `advertise()`; boot with stored value.
4. Wire `on_settings`; extend hello/settings JSON.
5. Bridge message types + log line `device bluetooth on/off`; store for status.
6. `idf.py build`, simulator screenshot, flash; `npm test`/`npm run build`.

## Success Criteria
- [ ] Fresh flash: Settings shows Bluetooth off; a Mac scan (`npm start` logs / macOS Bluetooth) does not see Watchling.
- [ ] Toggle on: bonded Mac reconnects; BT icon appears; unplug USB → BLE active ≤2 s, pending approval kept.
- [ ] Toggle off while connected: BLE drops within 1 s; USB unaffected; setting survives reboot.
- [ ] Bridge logs `ble` from `hello`/`settings`; older firmware without the field still works.

## Risk Assessment
- Behavior change for current BLE users (default off) → release note in Phase 5.
- Settings crowding on round display → simulator first; if it does not fit, put Voice and Bluetooth side by side as two small toggles (no hidden gestures).
- Calling GAP APIs from the LVGL task: verify thread safety; else defer to host task.
