# Configuration and local data

The bridge reads environment variables at startup. Settings on the device control brightness, volume, Voice and Bluetooth separately and persist in NVS. Bluetooth is off until switched on on the device.

## Runtime variables

| Variable | Default | Behavior |
| --- | --- | --- |
| `WATCHLING_PORT` | Auto-detect | Exact USB serial path; useful with multiple Espressif devices |
| `WATCHLING_BLE` | Enabled | Set exactly `off` to disable Bluetooth scanning on the Mac (the device's own switch controls advertising) |
| `WATCHLING_CONFIRM_TIMEOUT_S` | `120` | Seconds before an unanswered device permission request returns to the terminal |
| `WATCHLING_TTS` | `say` on macOS | `say` or `off`; off also disables chatter, leaving device chimes |
| `WATCHLING_TTS_VOICE` | System voice | macOS `say` voice for task and permission announcements; does not change prerecorded chatter |
| `WATCHLING_CHATTER` | Enabled | Set exactly `off` to disable idle chatter |
| `WATCHLING_CHATTER_DELAY_S` | Unset | Positive fixed delay before each chatter attempt, for development; normal timing is 10 minutes initially, then 10–20 minutes |

Use a positive finite permission timeout below the installed permission hook's 150-second timeout. Invalid numeric values are not configuration validation errors with a friendly message; keep values explicit and conservative.

There is no runtime `WATCHLING_CHATTER_VOICE` setting. Chatter uses bundled English clips. No `.env` loader is installed: placing variables in a file alone does not configure the bridge.

## Foreground process

From `pc-bridge/`:

```sh
WATCHLING_BLE=off WATCHLING_CHATTER=off npm start
```

The variables apply only to this process. To list available announcement voices:

```sh
say -v '?'
```

## Automatic startup with launchd

The launch agent does not inherit your interactive shell configuration. Set variables in the current macOS user launchd environment, then restart the bridge:

```sh
launchctl setenv WATCHLING_BLE off
cd pc-bridge
npm run bridge:stop
npm run bridge:start
```

`bridge:start` (or a new agent session) restarts it. `launchctl setenv` affects subsequently launched processes in that user session; it is not a persistent configuration file across logins. Remove an override with `launchctl unsetenv WATCHLING_BLE`, then restart again. Reinstall hooks after moving the repository or switching the Node executable.

## Local files

| Location | Purpose |
| --- | --- |
| `~/.watchling/bridge.sock` | Local Unix IPC socket for hooks and statusline |
| `~/.watchling/config.json` | Installer state including the original Claude statusline command |
| `~/.watchling/usage.json` | Cached usage and daily Claude spend |
| `~/.watchling/bridge.log` | launchd stdout/stderr logs |
| `~/Library/LaunchAgents/com.watchling.bridge.plist` | On-demand bridge registration |
| `~/.claude/settings.json.watchling-backup-*` | Claude settings snapshots from install/uninstall |
| `~/.codex/hooks.json.watchling-backup-*` | Codex settings snapshots from install/uninstall |

The state directory is created with mode `0700`. Do not delete `config.json` before restoring the original Claude statusline. Uninstall retains this state and settings backups.

## What the bridge reads and sends

Claude hooks send tool requests and completion/attention/error text through the local IPC socket. The statusline wrapper forwards Claude's status JSON and runs the original statusline command. Codex tracking reads local JSONL rollout files under `~/.codex/sessions` for usage, work state and completed task text.

The device receives agent source, usage percentages, estimated Claude cost, local clock offset, project basename, short permission summaries and shortened event text. Task and permission announcements can speak a summary aloud. Treat screenshots, device displays and logs as potentially containing private project or command information. The risk label is a pattern-based convenience, not a security boundary.

The runtime bridge does not call Gemini or ElevenLabs and does not require their credentials. It communicates locally with agent hooks, USB and BLE. Existing agents and an original custom statusline may have their own network behavior. Developer audio generators make external API calls and are described in [development](development.md).

Related: [installation side effects](getting-started.md#agent-integrations), [chatter behavior](chatter.md), [troubleshooting](troubleshooting.md).
