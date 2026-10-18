# Troubleshooting

Start with a foreground USB connection from `pc-bridge/`: `WATCHLING_BLE=off npm start`. This separates device connectivity from agent hook and Bluetooth setup. Stop any existing bridge first with `npm run bridge:stop`.

| Symptom | Check or recovery |
| --- | --- |
| No USB port | Use a data cable, reconnect while holding BOOT, then run `npm run list-ports` |
| Flash fails because port is busy | Stop bridge; exit `idf.py monitor` with Ctrl-]; close other serial tools |
| Wrong board selected | Set `WATCHLING_PORT` to the exact listed `/dev/cu.…` path |
| `idf.py` is missing | Source `scripts/idf-env.sh` from the root; set `IDF_PATH` for a nondefault installation |
| ESP-IDF helper cannot load | Complete ESP-IDF's installation first; check that its `export.sh` exists and supports your shell |
| Native Node module load error | Activate Node 22, run `npm ci` then `npm run build`; reinstall agent hooks to pin that Node executable |
| Hook install rejects agent settings | Ensure the existing settings or hooks file contains valid JSON in the supported object format |
| Bridge does not start with a session | Rebuild and reinstall the relevant hooks; begin a new session; inspect `~/.watchling/bridge.log` |
| Environment override appears ignored | A launchd process does not inherit shell exports; use `launchctl setenv` then restart the bridge |
| Claude usage unknown | Use terminal Claude Code with Watchling's statusline wrapper; desktop sessions do not provide this statusline feed |
| Codex usage unknown | A local rollout must exist under `~/.codex/sessions`; updates depend on the CLI's session JSONL format |
| No Codex permission modal | Install Codex hooks, trust them in Codex, and use a CLI version supporting permission hooks |
| Device sleeps or clock reads `--:--` | The bridge is not communicating yet; inspect connection logs and USB ownership |
| No battery indicator | Firmware hides it when the BQ27220 fuel gauge does not answer |
| No Wi-Fi indicator | Wi-Fi is not implemented |

## Bluetooth

Bluetooth must be enabled and macOS must allow access. Check that `WATCHLING_BLE=off` is not set in the process environment. The device advertises as Watchling and displays a six-digit pairing code; enter that code on the Mac when requested.

If pairing repeatedly fails after erasing firmware/NVS, remove the stale pairing on the Mac and pair again. First confirm that the same firmware works over USB. Native Bluetooth permissions can depend on how the bridge was started; the installed integration uses launchd to start it outside the coding agent's process tree.

## Sound and chatter

Device volume affects both chimes and speech. Voice in device Settings disables announcements and chatter; `WATCHLING_TTS=off` does the same in the bridge. `WATCHLING_CHATTER=off` disables only idle chatter. Speech audio is available only while the active connection is USB; BLE still supports chimes.

Announcements use macOS `say`, while chatter plays prerecorded English files. `WATCHLING_TTS_VOICE` changes announcements only. Missing or invalid chatter clips are skipped; startup logs report clip availability and invalid files. The runtime does not generate replacement clips.

Chatter waits 10 minutes after agent activity, then 10–20 minutes between attempts. It is silent between 22:00 and 08:00 local time, while an agent is busy, and after four lines in a quiet spell. A busy flag without a matching completion expires after 15 minutes. For development use a foreground run with `WATCHLING_CHATTER_DELAY_S=30`; quiet hours and other eligibility rules still apply. See [chatter](chatter.md).

## Permission fallback

No live device means the normal terminal permission prompt should appear. A disconnect or timeout also releases a pending device request. Read/answer questions in the terminal when they are not simple approval decisions. Do not increase the bridge timeout beyond the installed hook's 150 seconds.

## Recovering agent settings

Install/uninstall prints the settings path and timestamped backup. Compare that backup with the current JSON before restoring entries. Before moving the repository, uninstall the installed integrations from the old checkout. Then rebuild and reinstall from the new location. Ownership checks match the current checkout's exact script paths, so a new checkout does not remove hooks pointing to the old one. If the old location is already gone, inspect the relevant agent JSON and manually remove stale Watchling hook entries; restore the original Claude statusline command from `~/.watchling/config.json` if needed before installing again. Preserve hooks and statusline settings belonging to other tools.

The original Claude statusline command is stored in `~/.watchling/config.json`. Retain it until the wrapper has been removed. Do not post settings backups, session files or complete bridge logs publicly without reviewing them for private content.

When reporting a problem, include macOS, Node, ESP-IDF and agent versions, board revision, USB/BLE transport, sanitized logs and the commands that reproduce it. See [CONTRIBUTING.md](../CONTRIBUTING.md).
