# EchoEar Agent Client

Desk display for Claude Code and Codex on the **ESP-VoCat V1.2** (formerly EchoEar):
usage arcs, task notifications and Approve/Deny for Claude Code permission requests.

| Path | What |
|---|---|
| `echoear-firmware/` | ESP-IDF v5.5 firmware (BSP `espressif/esp_vocat`, LVGL 9) |
| `pc-bridge/` | Node 22 / TypeScript bridge between the agents and the device |
| `scripts/idf-env.sh` | Loads ESP-IDF with its Python 3.12 venv |
| `plans/` | Implementation plan and decisions |

## Firmware

```sh
. scripts/idf-env.sh            # once per terminal
cd echoear-firmware
idf.py build
idf.py -p /dev/cu.usbmodem* flash monitor
```

The firmware shows Claude Code / Codex usage arcs, today's Claude cost, the latest
agent event, and a touch Approve/Deny modal for Claude Code permission requests.

If the board doesn't show up as a USB port, hold **BOOT** while plugging in USB-C.

### Sound effects

The four UI sounds (`main/assets/sounds/`, cute-robot style) were generated with the ElevenLabs
Sound Effects API and are compiled into flash. To make new ones (needs `ELEVENLABS_API_KEY`):

```sh
cd echoear-firmware
python3 tools/sound_effects.py generate --style robot      # or scifi; see STYLES in the script
open tools/sfx-candidates/index.html                       # listen and pick
python3 tools/sound_effects.py embed confirm=robot:2 done=robot:1 attention=robot:1 error=robot:1
```

### Robot face

The robot head (`main/robot_face.c`) is one image with an empty visor
(`main/assets/robot_head.png`, generated with Codex image generation); the eyes are drawn by LVGL.
It idles (random blinks, glances), scans while Claude Code or Codex is working, sleeps without the
bridge, and reacts to events: happy on done, amber on waiting for input, `× ×` on errors, looking
toward the agent's arc (Claude left, Codex right). The working state needs the `UserPromptSubmit`
hook, so re-run `npm run hooks:install` after updating the bridge.

### Dashboard simulator

The idle dashboard (`main/agent_dashboard.c`) is plain LVGL, so it also builds on the host
against the same LVGL 9.6 the firmware uses (run `idf.py reconfigure` once so
`managed_components/` exists). Needs `brew install cmake pkg-config sdl2 imagemagick`.

```sh
cd echoear-firmware/simulator
./render-screenshot.sh /tmp/dash.png              # 360x360 PNG with the round mask
./render-screenshot.sh /tmp/dash.png --stress     # worst-case values; also --unknown, --after-ms N
./render-screenshot.sh /tmp/dash.png --mood happy # idle busy sleep happy attention error
./cmake-out/dashboard_sim                         # live SDL window, cycles through the robot moods
```

On the device, usage, cost, status, clock/date and the USB link icon come from the bridge
(the clock shows `--:--` until the first sync). Battery comes from the BQ27220 fuel gauge
(hidden if it does not answer); Wi-Fi/BLE icons stay hidden until those radios exist.

## PC bridge

```sh
cd pc-bridge
nvm use                         # Node 22
npm install && npm run build
npm run list-ports              # Espressif device is marked with *
npm start                       # bridge daemon; keep it running
npm run hooks:install           # adds hooks + statusline wrapper to ~/.claude/settings.json (backed up)
npm run hooks:uninstall         # removes them and restores the original statusline
npm run hooks:install:codex     # adds the permission hook to ~/.codex/hooks.json (backed up)
npm run hooks:uninstall:codex   # removes it
npm test
```

How it behaves:

- Permission requests appear on the device only while the bridge runs and the device answers
  pings. Otherwise Claude Code shows its normal terminal prompt immediately.
- While a request is on the device, the Claude Code terminal waits (spinner). Unanswered
  requests fall back to the terminal after `ECHOEAR_CONFIRM_TIMEOUT_S` (default 120).
- Codex usage, working and "done" events come from `~/.codex/sessions` with no Codex config.
  Approve/Deny for Codex needs `npm run hooks:install:codex` (Codex hooks, 0.162+); Codex asks you
  to trust the new hook once. The modal is titled with the agent that asks.
- Sound: the device chimes on permission requests, finished tasks, idle waits and errors, and reads
  permission requests and finished tasks aloud. Speech is made on the Mac with `say`:
  `ECHOEAR_TTS=off` mutes it (chimes stay). By default it uses the macOS system voice (System
  Settings → Accessibility → Spoken Content), which is the only way to get Siri voices; set
  `ECHOEAR_TTS_VOICE=Samantha` to force a voice listed by `say -v '?'`.

Wire format and message types: [docs/device-protocol.md](docs/device-protocol.md).
