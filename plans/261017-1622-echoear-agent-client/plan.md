# EchoEar (ESP-VoCat) Agent Client — Plan

Status: Phase 1 done (verified on device 2026-10-17); Phase 2 in progress
Created: 2026-10-17

## Locked decisions
- Board: ESP-VoCat (renamed EchoEar) V1.2, host macOS. Official BSP `espressif/esp_vocat` ^1.1.0
  (simpler than esp_board_manager; covers LCD/touch/ES8311/ES7210). BSP lacks BMI270 + BQ27220 →
  add drivers on the BSP I2C bus later.
- ESP-IDF v5.5.6 at ~/esp/esp-idf, Python 3.12 venv (system python 3.15 too new) → `scripts/idf-env.sh`. LVGL 9.6.
- PC bridge: TypeScript, Node 22 LTS (native `serialport`/`noble`).
- Claude Code integration via hooks (`PermissionRequest`, `Notification`, `Stop`, statusline) — no `node-pty`.
- Accounts: subscription (Claude Pro/Max, ChatGPT plan) → arcs show 5h/weekly `used_percentage`; handle missing fields.
- Codex scope: usage % + done notifications only (tail `~/.codex/sessions/**/*.jsonl`), no approval interception.
- Transport MVP: USB-Serial-JTAG, COBS + CRC16 framing. BLE (NUS, LESC passkey bonding, ADPCM) = separate later phase.
- TTS: no on-device English TTS (ESP-TTS is Mandarin-only) → prerecorded clips in flash + PC-generated PCM over USB.
- Safety policy: touch = approve/deny; voice = approve only low-risk, deny always; IMU tap = deny/mute only.
  Every CONFIRM_REQ carries `id` + `timeout`; mismatched id ignored; timeout → fall back to terminal prompt.

## Phases
1. Monorepo scaffold + hardware bring-up (display test screen, touch read, beep)
2. USB end-to-end: hook → bridge → confirm modal → touch approve → Claude proceeds
3. Full LVGL circular dashboard (arcs, chart, transitions)
4. Audio pipeline (ES8311/ES7210) + ESP-SR AFE (MMR) + MultiNet7 English
5. BLE transport

## Phase 1 acceptance (on device)
- Arcs render on left (orange 72%) / right (green 38%) edges, nothing clipped
- Tap dot lands under finger (no mirrored/swapped axes)
- PSRAM shows ~16384 KB
- Beep at boot and on each tap
- `npm run list-ports` marks the device (VID 303a)

## Open questions
- Company IT policy on USB CDC / BLE devices?
