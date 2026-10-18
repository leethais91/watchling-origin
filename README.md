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

Five UI sounds (`main/assets/sounds/`, "premium AI companion" style: warm glass-like synth notes)
were generated with the ElevenLabs Sound Effects API and are compiled into flash. They share a
note motif so you can tell them apart without looking:

| Sound | Motif | Plays when |
|---|---|---|
| `boot` | soft sweep, three rising notes | the device starts |
| `working` | three quiet pulses | an agent starts a task (at most once per 30 s) |
| `done` | three rising notes that resolve | a task finishes; also the volume sample in settings |
| `attention` | two rising notes, left hanging | a permission request, or an agent waiting for input |
| `error` | two falling notes | an API error |

Loudest to quietest: attention, done, error, boot, working. To make new ones (needs `ELEVENLABS_API_KEY`):

```sh
cd echoear-firmware
python3 tools/sound_effects.py generate --style premium    # 4 variants each; see STYLES in the script
open tools/sfx-candidates/index.html                       # listen and pick
python3 tools/sound_effects.py embed boot=premium:2 working=premium:2 done=premium:2 attention=premium:2 error=premium:2
```

### Robot face

The robot head (`main/robot_face.c`) is one image with an empty visor
(`main/assets/robot_head.png`, generated with Codex image generation); the eyes are drawn by LVGL.
It idles (random blinks, glances), scans while Claude Code or Codex is working, sleeps without the
bridge, and reacts to events: happy on done, amber on waiting for input, `> <` squints on errors,
looking toward the agent's arc (Claude left, Codex right). The working state needs the
`UserPromptSubmit` hook, so re-run `npm run hooks:install` after updating the bridge.

After 30 s with no agent event, permission request or touch, the dashboard gives way to face mode:
only the neon rim stays and the eyes fill the screen. After 5 quiet minutes the robot dozes off
(closed lids breathing, z's floating up). Any event or a tap brings the dashboard back; a tap
also startles the robot. Usage and clock updates don't count as activity.

### Settings

Pull down from the top edge to open settings: screen brightness
(left arc), speaker volume (right arc, plays a sample chime on release) and Voice (TTS) on/off.
To avoid opening it by accident, the drag must start at the very top and travel about half the
screen (a ring fills; release early and it springs back); taps and other swipes still poke the
robot. Swipe up, tap the hint, or wait 15 s to close. Values are kept in NVS. Turning the voice
off tells the bridge to stop speaking; chimes keep playing.

### Dashboard simulator

The idle dashboard (`main/agent_dashboard.c`) is plain LVGL, so it also builds on the host
against the same LVGL 9.6 the firmware uses (run `idf.py reconfigure` once so
`managed_components/` exists). Needs `brew install cmake pkg-config sdl2 imagemagick`.

```sh
cd echoear-firmware/simulator
./render-screenshot.sh /tmp/dash.png              # 360x360 PNG with the round mask
./render-screenshot.sh /tmp/dash.png --stress     # worst-case values; also --unknown, --after-ms N
./render-screenshot.sh /tmp/dash.png --mood happy # idle busy sleep happy attention error surprised
./render-screenshot.sh /tmp/face.png --face       # fullscreen face mode; combine with --mood
./render-screenshot.sh /tmp/set.png --settings    # settings screen; --pull N for the pull indicator
./cmake-out/dashboard_sim --ui                    # firmware UI; try the pull-down with the mouse
./cmake-out/dashboard_sim                         # live SDL window, cycles through the robot moods
```

On the device, usage, cost, status, clock/date and the USB link icon come from the bridge
(the clock shows `--:--` until the first sync). Battery comes from the BQ27220 fuel gauge
(hidden if it does not answer). The BLE icon shows while the bridge answers over Bluetooth;
the Wi-Fi icon stays hidden (no Wi-Fi yet).

## PC bridge

```sh
cd pc-bridge
nvm use                         # Node 22
npm install && npm run build
npm run list-ports              # Espressif device is marked with *
npm run hooks:install           # adds hooks + statusline wrapper to ~/.claude/settings.json (backed up)
                                #   and the bridge launch agent; run it with Node 22
npm run hooks:uninstall         # removes them, the launch agent, and restores the original statusline
npm run hooks:install:codex     # adds the session-start + permission hooks to ~/.codex/hooks.json
npm run hooks:uninstall:codex   # removes them
npm run bridge:stop             # stop the bridge, e.g. to free the USB port for idf.py flash
npm start                       # or run the bridge by hand in a terminal (logs to stdout)
npm test
```

How it behaves:

- The bridge starts on its own when a Claude Code or Codex session starts (`SessionStart` hook)
  and keeps running after the session ends. It runs as an on-demand launchd agent
  (`~/Library/LaunchAgents/com.echoear.bridge.plist`, not started at login, not restarted after
  `bridge:stop`) and logs to `~/.echoear/bridge.log`. launchd, not a child of the hook, because
  macOS kills a process started by Claude Code as soon as it touches Bluetooth. It does not see
  your shell's environment: set `ECHOEAR_*` variables with `launchctl setenv`, or use `npm start`.
  Re-run `npm run hooks:install` after switching Node versions or moving the repo.
- Permission requests appear on the device only while the bridge runs and the device answers
  pings. Otherwise Claude Code shows its normal terminal prompt immediately.
- While a request is on the device, the Claude Code terminal waits (spinner). Unanswered
  requests fall back to the terminal after `ECHOEAR_CONFIRM_TIMEOUT_S` (default 120).
- Bluetooth: the bridge also connects over BLE and uses it when the USB cable is out (battery).
  First time, the device shows a 6-digit code and macOS asks for it; after that it reconnects on
  its own. macOS may ask to allow Bluetooth (for the terminal app with `npm start`). Speech is
  USB-only (chimes still play over BLE). `ECHOEAR_BLE=off` keeps the bridge on USB only.
- Claude usage and today's cost come from the statusline, which only runs in terminal Claude Code
  sessions (not the desktop app). The last values are kept in `~/.echoear/usage.json`, so they
  survive bridge restarts; windows past their reset show 0%, and the cost starts over each day.
- Codex usage, working and "done" events come from `~/.codex/sessions` with no Codex config.
  Approve/Deny for Codex needs `npm run hooks:install:codex` (Codex hooks, 0.162+); Codex asks you
  to trust the new hook once. The modal is titled with the agent that asks.
- Sound: the device chimes on permission requests, finished tasks, idle waits and errors, and reads
  permission requests and finished tasks aloud. Speech is made on the Mac with `say`:
  `ECHOEAR_TTS=off` mutes it (chimes stay). By default it uses the macOS system voice (System
  Settings → Accessibility → Spoken Content), which is the only way to get Siri voices; set
  `ECHOEAR_TTS_VOICE=Samantha` to force a voice listed by `say -v '?'`.
- Chatter: when no agent has done anything for 5–10 minutes, the robot says a silly Vietnamese line
  (time of day, quota nearly gone, big spend, or just bored), at most 3 times per quiet spell and
  never while an agent works. Voice `ECHOEAR_CHATTER_VOICE` (default `Linh`, which macOS ships;
  if it is missing, `say` silently uses its default voice); `ECHOEAR_CHATTER=off` turns it off. It
  is also off whenever `ECHOEAR_TTS=off`, and USB-only like all speech. Each line is logged as
  `chatter: ...` in the bridge log; `ECHOEAR_CHATTER_DELAY_S=30` shortens the wait for trying it out.
  Character, line catalog and how to add lines: [docs/chatter-design.md](docs/chatter-design.md).

Wire format and message types: [docs/device-protocol.md](docs/device-protocol.md).
