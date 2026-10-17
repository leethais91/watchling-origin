# BLE transport — Plan

Status: done; verified on device 2026-10-17 (pairing, bonded reconnect, USB→BLE→USB switching,
Claude + Codex approvals over BLE) · Created: 2026-10-17

## Findings
- With BT on, the BSP's default 2 x 100-line LVGL buffers no longer fit in internal DMA RAM
  (boot loop in `bsp_display_lcd_init`); 60 lines fit. ~56 KB internal RAM free after boot:
  ESP-SR will need room found elsewhere.
- macOS holds the first write until the passkey is typed; pings must not queue up meanwhile.

## Decisions (user, 2026-10-17)
- No speech over BLE: chimes still play from flash; TTS clips go over USB only. ADPCM later,
  after measuring real BLE throughput.
- USB preferred when both links are up; BLE is the fallback when the cable is out.
- LESC passkey pairing with bonding; passkey shown on the device screen. No "Just Works":
  BLE can approve shell commands, so the link must be authenticated.

## Design
- GATT: Nordic UART Service (NUS) `6E400001-B5A3-F393-E0A9-E50E24DCCA9E`.
  RX `…0002` (host → device, write with response, needs encrypted + authenticated link),
  TX `…0003` (device → host, notify, only sent on an authenticated link).
- Payload: the same COBS + CRC16 byte stream as USB, split into ATT-sized chunks. Each link
  keeps its own frame assembler.
- Device TX goes to every connected link; the bridge only listens to its active link, so
  duplicates (e.g. `confirm_res`) are dropped there.
- Device shows the BT icon while an authenticated BLE connection exists; USB icon lights while
  the bridge talks over USB.

## Firmware
- `sdkconfig.defaults`: BT + NimBLE peripheral only, 1 connection, bonds in NVS, NimBLE memory
  in PSRAM. Regenerate `sdkconfig` (delete it; it is gitignored).
- `frame_link.c/.h`: frame assembly + encoding shared by USB and BLE (out of `host_link.c`).
- `ble_link.c/.h`: NimBLE init, NUS service, advertising (name + service UUID), security,
  passkey display, reconnect advertising.
- `host_link.c`: USB part only, uses frame_link; `host_link_send_json` fans out to USB + BLE.
- UI: passkey panel (`ui_show_passkey` / `ui_hide_passkey`), link probe returns transport
  (none/usb/ble), BLE icon state.
- `nvs_flash_init` at boot (bond storage).

## Bridge
- `@stoprocent/noble` (maintained CoreBluetooth binding).
- `transport/ble-transport.ts`: scan by service UUID, connect, subscribe TX, write RX with
  response in chunks, ping/pong liveness, reconnect.
- `transport/transport-hub.ts`: same interface as SerialTransport; active = USB if alive else
  BLE; sends `hello` on switch so the bridge re-syncs; forwards only active-link messages.
- Speaker gets a link that is alive only while USB is active.
- `ECHOEAR_BLE=off` disables BLE.

## Validation
- Unit tests for the hub's switching and message filtering.
- Firmware build + simulator (passkey panel).
- On device: pair from the Mac, approve a request over BLE with USB unplugged (power from
  battery), plug USB back and confirm the bridge switches back.

## Risks
- macOS asks for Bluetooth permission for the terminal running the bridge.
- Write-with-response is slow but our messages are small; speech is excluded.
- RAM: NimBLE ~60 KB; host memory moved to PSRAM.
