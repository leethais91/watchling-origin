---
title: USB/BLE connection reliability and on/off lifecycle
description: >-
  Fix USB/BLE link drops and lost approvals, make Bluetooth opt-in from device
  Settings, add bridge status/start, dim the device on battery when unlinked.
status: completed
priority: P2
branch: main
tags:
  - bridge
  - firmware
  - ble
  - usb
blockedBy: []
blocks: []
created: '2026-10-20T07:56:40.201Z'
createdBy: 'ck:plan'
source: skill
---

# USB/BLE connection reliability and on/off lifecycle

## Overview
Source: [brainstorm report](../reports/brainstorm-261020-1300-connection-lifecycle-report.md) (user-approved 2026-10-20).
Fix link reliability first (zombie USB port, approvals dropped on brief link loss, duplicate hello, BLE fallback that never connected), then give the user explicit control: a Bluetooth switch on the device (default off), `bridge:status`/`bridge:start`, and automatic screen-off on battery.

## Phases

| Phase | Name | Status |
|-------|------|--------|
| 1 | [Link reliability](./phase-01-link-reliability.md) | Completed |
| 2 | [Bluetooth toggle](./phase-02-bluetooth-toggle.md) | Completed |
| 3 | [Bridge status and start](./phase-03-bridge-status-and-start.md) | Completed |
| 4 | [Battery power saving](./phase-04-battery-power-saving.md) | Completed |
| 5 | [Docs and release notes](./phase-05-docs-and-release-notes.md) | Completed |

Order is strict: 1 → 2 → 3 (status reports the `ble` field from 2) → 4 → 5.

## Key decisions (user)
- BLE toggle lives in device Settings, NVS, **default off**; device is the source of truth.
- BLE on + USB plugged: BLE stays connected as standby.
- 10 s grace before pending approvals fall back to the terminal.
- Battery: not charging + no link for 10 min → backlight off; touch/motion/link wakes.
- No auto-exit for the bridge; no deep sleep/button power-off; no BLE device pinning.

## Acceptance (whole plan)
- Silent-but-open USB port recovers within 10 s.
- A ≤10 s link gap keeps the approval on the device; longer gaps return it to the terminal.
- BLE off → device not visible to a Mac scan; BLE on + unplug → BLE active ≤2 s.
- `npm run bridge:status` shows the active link, link liveness, device BLE setting, fw and pending requests.
- On battery with no link, the screen turns off after 10 min; never on USB power.
- `npm test`, `npm run build` (pc-bridge), `idf.py build` and simulator pass.

## Dependencies
No overlap with in-progress plans (`261018-1405-chatter-v3`, `261018-public-repository`, `261019-2221-firmware-web-release`).

## Outcome (2026-10-20)
All phases done and verified on device: USB/BLE switching both ways with an approval on screen, Bluetooth switch (off by default, persists across reboot), `bridge:status`/`bridge:start`, screen-off on battery (tested with a 1-min build).
BLE root cause found on device: other Nordic UART devices nearby matched the scan and CoreBluetooth connects never time out; fixed by matching the Watchling name plus a 10 s connect timeout.

Open (not done): face-down quiet mode wakes a dark screen; any IMU event wakes it (check desk vibration); 10-s grace path only unit-tested (switches had no gap).

## Unresolved questions
- Why BLE never connected in the 2026-10-20 12:30 session (Phase 1 debug).
- Bridge restart churn on 12:38–12:41 (Phase 1 debug).
- Whether opening the serial port resets the ESP32-S3 via DTR/RTS.
