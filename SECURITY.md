# Security and data handling

Watchling handles coding-agent permission requests. Report problems involving unintended
approval, forged decisions, access to the local bridge socket or BLE pairing as security issues.
This project does not currently promise a security response SLA or a supported-release window.

## Reporting

If the GitHub repository has private vulnerability reporting enabled, use its **Security →
Report a vulnerability** page. Otherwise, open an issue requesting a private contact channel
without exploit details or sensitive data; wait for a maintainer to provide one. Do not post
credentials, private session files or executable exploits in public issues.

## Local data and permissions

- Claude hooks read agent event payloads; the statusline provides usage and today's cost.
- The Codex watcher reads local session files under `~/.codex/sessions` to obtain activity and usage.
- The bridge stores configuration, cached usage and logs in `~/.watchling/`. Treat logs and
  backups as personal data when sharing diagnostics.
- Installation changes `~/.claude/settings.json` or `~/.codex/hooks.json`, creates timestamped
  backups of existing files and installs `~/Library/LaunchAgents/com.watchling.bridge.plist`.
- The local IPC socket uses file permissions `0600`, and the bridge creates its state directory
  with mode `0700`. These are implementation protections, not a claim of a completed security audit.
- Permission summaries and project names are sent to the device. Spoken notifications may be
  audible to other people. Disable voice in Settings or with `WATCHLING_TTS=off` if needed.
- The normal bridge uses local files, USB/BLE and macOS speech synthesis. The optional voice/SFX
  generation tools send generation text/prompts to external providers and require separate API keys.

Unanswered requests and device disconnections fall back to the agent's terminal prompt.
The bridge accepts approval by touch; physical tap decisions can only deny. Other decision
sources are restricted by the bridge's risk policy. These controls do not make a paired or
physically accessible device a substitute for reviewing a requested command.

For installation, rollback and log locations, see [getting started](docs/getting-started.md)
and [troubleshooting](docs/troubleshooting.md).
