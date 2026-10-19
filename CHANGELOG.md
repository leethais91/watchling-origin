# Changelog

Changes below are unreleased; no public release is implied.

## Unreleased

### Added

- ESP-VoCat V1.2 firmware with agent usage arcs, permission requests, animated expressions and settings.
- macOS bridge for Claude Code and Codex events over USB/BLE.
- Local spoken notifications and pre-generated English idle chatter clips.
- Watchling brand assets, public setup/reference guides and contribution templates.
- MIT License for the project's source code.
- CI for bridge checks, firmware compilation and repository credential-pattern checks.

### Changed

- ESP-IDF helper uses the configured SDK location (`IDF_PATH`) and pins a uv-managed Python
  (default 3.12, override with `WATCHLING_IDF_PYTHON`) instead of a machine-specific setup.
- Claude Code and Codex share the bridge launch agent until the last integration is uninstalled.
- Hook install/uninstall warns about entries pointing to a missing `pc-bridge/dist` script, such as
  from a moved checkout, and leaves them for manual removal.
