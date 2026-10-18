# Contributing to Watchling

Watchling has an ESP-IDF/LVGL firmware and a Node 22/TypeScript bridge. Start with the
[getting started guide](docs/getting-started.md) and [architecture](docs/architecture.md).
The supported host is macOS; Linux/Windows support is not currently promised.

## Local checks

Use Node 22, then run:

```sh
cd pc-bridge
nvm use
npm ci
npm run typecheck
npm test
npm run build
```

From the repository root, with ESP-IDF 5.5 installed:

```sh
. scripts/idf-env.sh
cd echoear-firmware
idf.py build
```

Run `python3 scripts/check-secrets.py --history` from the repository root before sharing
a branch. This is a targeted pattern scan, not a full security audit.

## Changes and pull requests

- Keep a change focused; describe the user-visible problem and resulting behavior.
- Follow nearby TypeScript/C conventions. Keep protocol definitions in the bridge and firmware aligned.
- Add regression tests for changed bridge behavior. For firmware, describe the device or simulator checks you ran.
- Preserve other tools' hooks and the user's existing statusline. Exercise install/uninstall using fixtures; never point automated tests at your real agent settings.
- Update the relevant guides when configuration, hooks, transport behavior or setup changes.
- Keep API keys out of commits. Voice and sound generation are optional developer operations; normal use does not need those keys.
- Document asset sources in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

Use the pull request template to report actual checks. If you could not test on hardware, say so.
See [development](docs/development.md) for simulator and asset-generation details and
[SECURITY.md](SECURITY.md) for reporting security issues.

## Licensing

The owner is choosing the project's source license before publication. See the release
checklist for that decision; third-party notices remain separate. This paragraph will be
updated when the source license is selected.
