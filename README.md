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

Current firmware is a hardware bring-up: two usage arcs on the edge, a dot that
follows taps, PSRAM size on screen, and a beep at boot and on every tap.

If the board doesn't show up as a USB port, hold **BOOT** while plugging in USB-C.

## PC bridge

```sh
cd pc-bridge
nvm use                         # Node 22
npm install
npm run list-ports              # Espressif device is marked with *
```
