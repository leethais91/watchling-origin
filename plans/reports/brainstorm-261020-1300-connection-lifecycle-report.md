# Brainstorm: USB/BLE connection reliability and on/off lifecycle

Date: 2026-10-20 · Status: approved by user · Modes: none (no --html/--wiki)

## Problem
User pain points (all confirmed): unplugging the cable loses the connection; link switching is slow or flaky; BLE drops or never connects; there is no way to turn things on or off on purpose. The user wants explicit control: BLE only when allowed (opt-in), plus on/off for the bridge, Bluetooth and device power.

## Evidence (`~/.watchling/bridge.log`, code)
- `12:30:10 USB link open` followed by 3.5 min of silence before `12:33:45` on another port (`11301`). `SerialTransport.connectLoop` only reopens after a `close` event, so a port that stays open while the device is silent is never retried.
- `12:34:12 device unreachable` with no BLE line at all that session. BLE fallback never happened; root cause unknown (no scan/state logs).
- `1:40:19 unreachable → 1:40:23 talking over BLE` (also at 2:09): BLE reconnects in about 4 s, but `transport.on("disconnected")` → `broker.cancelAll()` already dropped the pending approvals.
- `device ready` always logged twice: the transport sends `hello` on open (`serial-transport.ts`, `ble-transport.ts`) and the hub sends another on switch (`transport-hub.ts`).
- Bridge restarted 3 times in 4 min on one day (12:38–12:41): cause unknown.
- Firmware advertises BLE whenever it is not connected and has no runtime off switch. `WATCHLING_BLE=off` exists only as a bridge env var.

## Decisions
| Topic | Decision |
|---|---|
| BLE toggle location | Device Settings switch, stored in NVS; device is the source of truth |
| BLE default | **Off** (opt-in); existing bonds kept |
| BLE on + USB plugged | BLE stays connected as standby so unplugging switches instantly |
| Approval grace on link loss | 10 s before `cancelAll` |
| Device power | Auto: on battery with no link for 10 min → backlight off, slower UI tick, slower advertising; touch/IMU/link wakes |
| Bridge lifecycle | Add `bridge:status`, `bridge:start`; keep `bridge:stop`; no auto-exit (YAGNI) |
| Order | Reliability first, then toggle, status, power |

## Approaches considered
- BLE toggle: device Settings (chosen: real battery saving, intent lives with the device, no state sync) vs Mac CLI (device keeps advertising and draining battery) vs both (most work).
- Power: automatic software screen-off (chosen, low risk) vs plus button deep sleep (BSP buttons are ADC channels 6/7; deep-sleep wake likely needs an RTC GPIO, unverified) vs defer.

## Final design
### Phase 1: Link reliability
- `serial-transport.ts`: if the port is open but no frame arrives for about 8 s, close it and re-detect the path.
- `bridge.ts`: on `disconnected`, start a 10 s timer before `broker.cancelAll()`; reconnecting cancels the timer and calls `resendActive()` (it already runs on the device `hello`).
- Hello ownership: the hub sends `hello` on switch; transports send `ping` on open (keep the BLE hello-until-first-answer behavior for pairing).
- Add BLE diagnostics (adapter state, scan start, found/not found, connect stages), then root-cause on device with `/ck:debug` why BLE did not fall back and why the bridge restarts. Fixes from the debug are in scope.
- Acceptance: a silent-port scenario recovers within 10 s; a 4 s BLE drop keeps the approval on the device; a drop over 10 s returns it to the terminal; one `device ready` per switch.

### Phase 2: Bluetooth toggle
- Firmware: Settings switch "Bluetooth"; NVS key; off → `ble_gap_terminate` + stop advertising (NimBLE stays initialized, no RAM change); on → advertise.
- Protocol: `ble: bool` in device `hello` and `settings`; update `docs/device-protocol.md` and `messages.ts`.
- UI: the round 360 px Settings already has 2 arcs + Voice; validate the layout in the simulator first.
- Acceptance: BLE off → Mac scan does not see Watchling; BLE on + unplug → BLE active within 2 s with the approval kept; BLE off + unplug → existing offline/sleep face.

### Phase 3: Bridge status/start
- IPC `/status` → `npm run bridge:status` prints: running, active link, USB/BLE alive, device BLE setting, fw, pending requests. Not running → clear message.
- `npm run bridge:start` → `launchctl kickstart` of `com.watchling.bridge`.

### Phase 4: Battery power saving
- Condition: fuel gauge reports not charging AND no host link for 10 min.
- Action: backlight off, slower LVGL/face timers, longer advertising interval when BLE is on.
- Wake: touch, IMU motion, link restored. On USB power: never.

## Touchpoints
Bridge: `pc-bridge/src/transport/{serial-transport,ble-transport,transport-hub}.ts`, `bridge.ts`, `ipc/ipc-server.ts`, `ipc/bridge-process.ts`, `stop-bridge.ts` (pattern for new scripts), `protocol/messages.ts`, `package.json` scripts, tests in `transport-hub.test.ts`, `permission-broker.test.ts`.
Firmware: `main/ble_link.c/.h`, `main/settings_screen.c`, `main/settings_store.c`, `main/main.c` (hello/settings JSON), `main/ui.c` (backlight/sleep), `main/battery.c`, simulator.
Docs: `hardware.md`, `troubleshooting.md`, `configuration.md`, `device-protocol.md`, `architecture.md`, `README.md` ("Connects over USB or paired Bluetooth LE").

## Risks
- BLE root cause may live in macOS/noble (CoreBluetooth state after sleep/wake) and need a larger fix than expected.
- Default-off BLE changes behavior for current BLE users → call it out in CHANGELOG/release notes.
- Backlight-off detection relies on the BQ27220; if the gauge is missing (battery hidden), power saving must stay disabled.
- Serial reopen could reset the board via DTR/RTS on open; verify on device.

## Out of scope
Speech over BLE, Wi-Fi, deep sleep/button power-off, pinning BLE to one device when several Watchlings are nearby, Linux/Windows.

## Next steps
`/ck:plan --tdd` with this report: Phase 1 changes existing transport/broker behavior covered by `transport-hub.test.ts` and `permission-broker.test.ts`.

## Unresolved questions
- Why BLE never connected in the 12:30 session (needs device repro).
- Cause of the bridge restart churn on 12:38–12:41.
- Whether opening the serial port toggles DTR/RTS and resets the ESP32-S3.
