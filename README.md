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

### Dashboard simulator

The idle dashboard (`main/agent_dashboard.c`) is plain LVGL, so it also builds on the host
against the same LVGL 9.6 the firmware uses (run `idf.py reconfigure` once so
`managed_components/` exists). Needs `brew install cmake pkg-config sdl2 imagemagick`.

```sh
cd echoear-firmware/simulator
./render-screenshot.sh /tmp/dash.png              # 360x360 PNG with the round mask
./render-screenshot.sh /tmp/dash.png --stress     # worst-case values; also --unknown, --after-ms N
./cmake-out/dashboard_sim                         # live SDL window (blink animation)
```

On the device, usage, cost, status, clock/date and the USB link icon come from the bridge
(the clock shows `--:--` until the first sync); Wi-Fi, BLE and battery show mock values until
those providers exist.

## PC bridge

```sh
cd pc-bridge
nvm use                         # Node 22
npm install && npm run build
npm run list-ports              # Espressif device is marked with *
npm start                       # bridge daemon; keep it running
npm run hooks:install           # adds hooks + statusline wrapper to ~/.claude/settings.json (backed up)
npm run hooks:uninstall         # removes them and restores the original statusline
npm test
```

How it behaves:

- Permission requests appear on the device only while the bridge runs and the device answers
  pings. Otherwise Claude Code shows its normal terminal prompt immediately.
- While a request is on the device, the Claude Code terminal waits (spinner). Unanswered
  requests fall back to the terminal after `ECHOEAR_CONFIRM_TIMEOUT_S` (default 120).
- Codex usage and "done" events come from `~/.codex/sessions`; no Codex config changes.

Wire format and message types: [docs/device-protocol.md](docs/device-protocol.md).
