# Settings screen — Plan

Status: done (verified on device) · Created: 2026-10-18

Pull-down settings screen on the device: screen brightness, speaker volume, spoken voice (TTS) on/off.

## Decisions (user, 2026-10-18)
- Mockups generated with Codex image generation (`mockups/`, prompt in `codex-prompt.txt`).
- Opening: pull down from the top edge (flow 1 + 2). Guards against accidental opening:
  - a grabber pill at 12 o'clock hints where to pull;
  - the drag must **start** in the top ~56 px strip; touches elsewhere still poke the robot;
  - the drag must pass a threshold (~half the screen); a ring around a gear fills while pulling,
    releasing early springs back;
  - never opens while the permission modal or pairing screen shows.
- Layout A, arc dials (`mockups/settings-a-arcs.png`): brightness arc on the left, volume arc on
  the right, Voice (TTS) switch in the middle, "Swipe up to close" at the bottom.

## Behaviour
- Closes on swipe up (outside the arcs), a tap on the close hint, 15 s without a touch, or when a
  permission request arrives.
- Brightness applies live while dragging, clamped to ≥ 10 % so the screen never goes dark.
  Volume applies on release and plays the "done" chime as a preview.
- Values persist in NVS (namespace `settings`); defaults brightness 100, volume 60, voice on.
- Voice off: the device drops speech clips and tells the bridge (`settings` message, also in
  `hello`), so the bridge skips synthesis. Chimes keep playing.

## Files
- `echoear-firmware/main/settings_screen.{c,h}` (new, pure LVGL, also in the simulator)
- `echoear-firmware/main/settings_store.{c,h}` (new, NVS)
- `echoear-firmware/main/ui.{c,h}`, `main.c`, `audio.{c,h}`, `CMakeLists.txt`
- `echoear-firmware/simulator/` (`--settings`, `--pull N`)
- `pc-bridge/src/protocol/messages.ts`, `pc-bridge/src/bridge.ts`
- `docs/device-protocol.md`, `README.md`

## Validation
- Simulator screenshots: dashboard with grabber, mid-pull, settings open.
- `idf.py build`; `npm run build && npm test` in `pc-bridge`.
- On device: flash, try accidental swipes, adjust each setting, reboot to check persistence.
