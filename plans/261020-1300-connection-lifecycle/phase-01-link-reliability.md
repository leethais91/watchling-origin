---
phase: 1
title: Link reliability
status: completed
priority: P1
dependencies: []
effort: 1-2 d (debug part open-ended)
---

# Phase 1: Link reliability

## Overview
Bridge-side fixes for evidence in `~/.watchling/bridge.log`: zombie USB port, approvals cancelled on brief link loss, duplicate `hello`, plus diagnostics and an on-device root cause for BLE fallback that never connected and for bridge restart churn.

## Requirements
- USB port that stays open while the device is silent is closed and re-detected.
- Losing every link waits 10 s before `broker.cancelAll()`; reconnecting within 10 s keeps the active request (re-sent on device `hello`).
- One `hello` round-trip per link switch (one `device ready` log line).
- BLE connect loop logs adapter state, scan start, peripheral found, connect/subscribe stages.
- Non-functional: no behavior change for a healthy USB link; no new dependencies.

## As built (2026-10-20, code part done; on-device steps 7–8 open)
- Grace lives in `PermissionBroker.cancelAllAfter(ms)` / `keepAll()`; `bridge.ts` calls them on hub `disconnected`/`connected`.
- BLE no longer sends pre-answer hellos at all; hub `hello` is never skipped by the pending-write ping filter.
- Stale check: `isLinkStale()` in `device-link.ts`; a failed close drops the port via `dropPort()`; serial `error` listener added.
- BLE write queue resets on disconnect. Logs: scan start, found (name/rssi), disconnect reason.
- Review: no blockers; open item = DTR/RTS reset check and possible reopen backoff for a silent device in download mode.
- On device 2026-10-20 18:11–18:16: BLE-only start connects in ~1 s; BLE→USB (18:15:00), USB→BLE (18:16:23, no `unreachable` gap), USB again (18:16:43); one `device ready` per switch; no boot log after a reopen (no DTR/RTS reset). Grace path not exercised (switch had no gap).
- Old-log findings: restart churn = deliberate `bridge:stop`/hooks reinstall, not crashes. BLE went silent for hours after `connect failed: cleanup` (4 times); suspected `connectAsync` hang (CoreBluetooth connects never time out) — unproven; new scan/found logs will show it if it recurs.

## Architecture (original design)
- `SerialTransport`: track `openedAtMs`/`lastSeenMs`; in the ping interval, if the port is open and `now - max(lastSeenMs, openedAtMs) > STALE_PORT_MS` (8 s), call `port.close()`. The existing `close` handler resets state and restarts `connectLoop()`, which re-runs `findDevicePath()` (handles re-enumeration to a new `/dev/cu.*`).
- Grace: owned by `bridge.ts`. On hub `disconnected` → start `LINK_LOSS_GRACE_MS = 10_000` timer → `broker.cancelAll()`. On hub `connected` → clear the timer. During the gap new permission requests still return `null` immediately (`transport.alive` false), unchanged.
- Hello ownership: hub sends `hello` on switch. `SerialTransport.open` sends `ping` instead of `hello`. `BleTransport` keeps hello-until-first-answer (pairing safety) but its `open()` sends `ping`.
  - Check the first-connect path: a link becomes alive only after the device answers a ping (`pong`), then hub switches and sends `hello` → device `hello` → resync. Confirm the device answers `ping` before `hello` (it does: `main.c` handles `ping` independently).

## Related Code Files
- Modify: `pc-bridge/src/transport/serial-transport.ts`, `pc-bridge/src/transport/ble-transport.ts`, `pc-bridge/src/bridge.ts`
- Modify (tests): `pc-bridge/src/transport/transport-hub.test.ts`
- Create (tests): `pc-bridge/src/transport/serial-transport.test.ts` only if the stale check can be tested without a real port (extract a pure `isStale(now, openedAt, lastSeen)` helper); otherwise cover via the helper in `device-link.ts`.
- Possibly modify after debug: `ble-transport.ts`, `ipc/bridge-process.ts`, firmware `ble_link.c`.

## Implementation Steps
1. Add `STALE_PORT_MS` + stale check in `SerialTransport`; log `"USB port silent; reopening"`.
2. Switch transport `open()` hellos to pings; keep BLE pre-answer hello in the interval.
3. Add grace timer in `bridge.ts` around `broker.cancelAll()`; log `"device unreachable; releasing requests in 10 s"` and `"link back; keeping requests"`.
4. Extract grace logic into a small testable unit if `bridge.ts` top-level code makes it untestable (e.g. `createLinkLossGrace(onExpire, ms)` next to the hub); test: expire after 10 s, cancelled by reconnect.
5. Hub test: switch emits exactly one `hello` to the new link.
6. Add BLE stage logs in `connectLoop`/`open`.
7. On device (`/ck:debug`): reproduce (a) USB unplug with BLE paired → does BLE connect? capture noble state/scan logs + firmware `ble_link` logs; (b) Mac sleep/wake and Bluetooth off/on; (c) launchd restart churn: `log show --predicate 'subsystem == "com.apple.xpc.launchd"'` around a SessionStart, check `bridge.log` for crash stack. Fix proven causes in this phase; record findings in `reports/`.
8. Verify DTR/RTS on open does not reset the board (watch for boot log after reopen); if it does, set `hupcl: false` / avoid toggling.

## Success Criteria
- [ ] Unit tests: stale detection, grace expire/cancel, single hello per switch.
- [ ] On device: silent port recovers ≤10 s; 4 s BLE drop keeps approval; >10 s drop returns it to terminal.
- [ ] On device: unplug USB with BLE paired → `talking to the device over BLE` within 5 s, or a documented root cause + fix.
- [ ] `npm test` and `npm run build` pass.

## Risk Assessment
- Closing a port mid-flash: user stops the bridge before flashing (documented), so no new risk.
- Grace extends how long an agent can wait on a dead device by ≤10 s; accepted by user.
- BLE root cause may sit in noble/CoreBluetooth; timebox debug to 1 day, then report options instead of patching blindly.
