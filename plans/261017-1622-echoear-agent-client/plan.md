# EchoEar (ESP-VoCat) Agent Client — Plan

Status: Phase 1 + Phase 2 done (verified on device 2026-10-17); Phase 3 next

## Phase 3 progress (2026-10-17)
- Idle dashboard screen done in `main/agent_dashboard.c`, verified in the host simulator
  (`echoear-firmware/simulator/`); firmware builds. Not yet checked on the panel.
- Avatar is cut from the design mockup (`main/assets/robot_avatar.png`, RGB565A8); eyes blink via
  LVGL lids over the baked-in eyes. Fonts: Montserrat Bold/SemiBold subsets (OFL).
- Clock/date synced from the bridge (`time` message). Wi-Fi/BLE/battery are mock until providers exist (no Wi-Fi, BLE or
  fuel-gauge driver yet). Working/notification screens, page dots and waveform not built.

## Phase 4a: sound + TTS (2026-10-17)
- Speaker side done: synthesized chimes per event kind (`main/audio.c`, replaces beep) and English
  TTS made on the host (`pc-bridge/src/tts/`, macOS `say` behind a `TtsEngine` interface for a
  cloud engine later), streamed as `say` + `0x02` PCM frames + `say_end`, played after the chime.
- Spoken: permission requests and finished tasks. Idle waits / errors: chime only.
- Still open from Phase 4: mic (ES7210), ESP-SR AFE, MultiNet voice commands.

## Phase 2 findings (live test 2026-10-17)
- While the PermissionRequest hook waits, the Claude Code terminal shows only a spinner (no
  parallel dialog). An unanswered request blocks the session until ECHOEAR_CONFIRM_TIMEOUT_S
  (default 120 s), then falls back to the terminal prompt. Revisit the default if this hurts.
- Live session: Bash request approved by touch end-to-end. Deny path covered by unit tests only.
- Daily cost counts only sessions whose statusline reached the bridge while it was running.
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
