# Architecture

Watchling has two runtime parts: firmware on ESP-VoCat and a local Node.js bridge on macOS. Agents communicate with the bridge through hooks/statusline IPC or local Codex session files.

```text
Claude Code hooks + statusline ── Unix socket ─┐
Codex CLI permission/session hooks ────────────┤
Codex CLI local session JSONL ─── file watcher ┤
                                             ▼
                                    macOS Node 22+ bridge
                                      │           │
                                 USB preferred   BLE fallback
                                      └─────┬─────┘
                                            ▼
                              ESP-VoCat / ESP32-S3 firmware
                              LVGL UI · touch · audio · NVS
```

## Bridge

`pc-bridge/src/bridge.ts` wires together the transports, usage store, permission broker, local IPC server, speech engine and chatter scheduler. Hooks and statusline use `~/.watchling/bridge.sock`. Automatic startup uses a shared on-demand launchd job; a new agent session starts it and it continues after the session ends.

Claude provides usage and estimated cost through its terminal statusline. Codex's local rollout watcher tracks usage, activity and completed tasks. Unknown measurements remain nullable; cached usage survives restarts. The clock is sent on device handshake and every ten minutes.

The transport hub selects one responsive link, preferring USB. Both links use the same COBS/CRC framed JSON protocol. Switching links triggers one handshake and state resync. A USB port that stays silent for 8 s is reopened; BLE only connects to a device advertising the Watchling name, with a 10 s connect timeout. Bluetooth is opt-in on the device. Audio PCM uses USB only. `npm run bridge:status` reads a snapshot over the IPC socket. See [device protocol](device-protocol.md) for the wire contract.

## Permissions

A permission hook posts a request to the local broker. The bridge derives a short tool summary and risk label, queues requests and displays one active request. A touch response matching the active request resolves the hook with allow/deny. Timeout, hook cancellation or loss of connectivity for more than 10 s releases the request without a device decision so the agent can use its normal prompt.

This is a local convenience interface for reviewing agent permissions. Risk classification is a regex/tool-name heuristic. The protocol defines voice/tap decision restrictions; those fields do not imply that voice recognition or tap recognition is a supported input feature.

## Firmware

`echoear-firmware/main/` contains startup, framed transport handling, USB/BLE, audio, dashboard and animated robot face. The ESP-VoCat BSP supplies board-specific display, touch and codec setup. Firmware uses LVGL 9.6 and ESP-IDF 5.5 defaults for the ESP32-S3 target.

Touch drives permission buttons and the pull-down settings screen. Brightness, volume, Voice and Bluetooth are stored in NVS. On battery with no USB power and no link for ten minutes the backlight turns off and advertising slows. Without recent activity the dashboard transitions into face mode and then dozing; incoming usage/clock updates do not count as interactions. Built-in chimes remain available independently of USB speech.

## Audio

Task and permission announcements use macOS `say`. Idle chatter loads bundled Gemini-generated English WAV clips plus optional ElevenLabs mood sounds. The bridge sends 16 kHz mono signed 16-bit PCM with optional face cues; the firmware plays it through the board's speaker. Normal operation does not call those audio-generation APIs.

Read [configuration](configuration.md) for data boundaries, [development](development.md) for tooling, and [chatter](chatter.md) for the maintained behavior reference.
